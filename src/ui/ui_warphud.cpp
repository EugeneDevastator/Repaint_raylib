#include "repaint.h"
#include "RaylibUtils.h"
#include "layerstack.h"
#include "viewport_manager.h"
#include "render_utils.h"
#include "rlgl.h"
#include "imgui.h"
#include "external/glad.h"
#include <math.h>
#include <string.h>

extern bool layersDirty;

#define DEG2RAD_F 0.01745329252f

// ── Model ─────────────────────────────────────────────────────────────
// Hierarchy (strict, no circular):
//   1. Center (C) — core. Nothing modifies it except direct drag.
//   2. Axes (L,R,T,B) — always pass through C. Cannot move C.
//   3. Corners — placed on rect sides. Cannot move C or axes.
//
// Axes: L/R collinear with C, T/B collinear with C.
//   Dragging an axis endpoint rotates/stretches the line around C.
//   The opposite endpoint projects onto the new line (keeps its distance).
//
// Corners: each stored as 2 scalars along adjacent axis directions.
//   TL = C + a*uL + b*uT   (stored: a,b)
//   BR = C + c*uR + d*uB   (stored: c,d)
//   TR = C + c*uR + b*uT   (computed from BR's c and TL's b)
//   BL = C + a*uL + d*uB   (computed from TL's a and BR's d)
// Dragging TL → updates a,b. Dragging BR → updates c,d.
// Dragging TR → updates c,b. Dragging BL → updates a,d.

static Vector2 g_center  = {0,0};
static Vector2 g_left    = {0,0};
static Vector2 g_right   = {0,0};
static Vector2 g_top     = {0,0};
static Vector2 g_bottom  = {0,0};

// Stored scalars (TL: a,b  BR: c,d). TR and BL computed.
static float g_sa = 0, g_sb = 0, g_sc = 0, g_sd = 0;
static Vector2 g_corners[4] = {{0,0},{0,0},{0,0},{0,0}};

// Drag state: 0=C,1=L,2=R,3=T,4=B,5=TL,6=TR,7=BR,8=BL
static int    g_dragWhich = -1;
static Vector2 g_dragOfs  = {0,0};
static int    g_hoverWhich = -1;

static RenderTexture2D g_warpRT = {0};
static int g_warpW = 0, g_warpH = 0;
static float g_aspectRatio = 1.0f;
static bool g_warpValid = false;
static bool g_warpDirty = true;
static bool g_needReset = true;

static Shader g_warpShader = {0};
static int locSrcTex=-1, locSrcSize=-1, locDstSize=-1;
static int locRow0=-1, locRow1=-1, locRow2=-1;
static Texture2D g_whiteTex = {0};

// ── Solvers ───────────────────────────────────────────────────────────

static bool Solve8x8(float A[64], float b[8], float x[8]) {
    for (int col = 0; col < 8; col++) {
        int best = col;
        for (int row = col + 1; row < 8; row++)
            if (fabsf(A[row * 8 + col]) > fabsf(A[best * 8 + col]))
                best = row;
        if (fabsf(A[best * 8 + col]) < 1e-12f) return false;
        if (best != col) {
            for (int j = col; j < 8; j++) { float t = A[col * 8 + j]; A[col * 8 + j] = A[best * 8 + j]; A[best * 8 + j] = t; }
            float t = b[col]; b[col] = b[best]; b[best] = t;
        }
        float piv = A[col * 8 + col];
        for (int row = col + 1; row < 8; row++) {
            float f = A[row * 8 + col] / piv;
            for (int j = col; j < 8; j++) A[row * 8 + j] -= f * A[col * 8 + j];
            b[row] -= f * b[col];
        }
    }
    for (int i = 7; i >= 0; i--) {
        float sum = b[i];
        for (int j = i + 1; j < 8; j++) sum -= A[i * 8 + j] * x[j];
        x[i] = sum / A[i * 8 + i];
    }
    return true;
}

static bool InvertMatrix3(const float H[9], float Hinv[9]) {
    float a=H[0],b=H[1],c=H[2],d=H[3],e=H[4],f=H[5],g=H[6],h=H[7],i=H[8];
    float det=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if (fabsf(det)<1e-12f)return false;
    float id=1.0f/det;
    Hinv[0]=(e*i-f*h)*id; Hinv[1]=-(b*i-c*h)*id; Hinv[2]=(b*f-c*e)*id;
    Hinv[3]=-(d*i-f*g)*id; Hinv[4]=(a*i-c*g)*id; Hinv[5]=-(a*f-c*d)*id;
    Hinv[6]=(d*h-e*g)*id; Hinv[7]=-(a*h-b*g)*id; Hinv[8]=(a*e-b*d)*id;
    return true;
}

static bool SolveHomography(const Vector2 src[4], const Vector2 dst[4], float H[9]) {
    float A[64], b[8];
    for(int i=0;i<4;i++){
        float x=src[i].x,y=src[i].y,xp=dst[i].x,yp=dst[i].y;
        int r0=i*2,r1=i*2+1;
        A[r0*8+0]=x;A[r0*8+1]=y;A[r0*8+2]=1;A[r0*8+3]=0;A[r0*8+4]=0;A[r0*8+5]=0;A[r0*8+6]=-xp*x;A[r0*8+7]=-xp*y;b[r0]=xp;
        A[r1*8+0]=0;A[r1*8+1]=0;A[r1*8+2]=0;A[r1*8+3]=x;A[r1*8+4]=y;A[r1*8+5]=1;A[r1*8+6]=-yp*x;A[r1*8+7]=-yp*y;b[r1]=yp;
    }
    float x[8]; if(!Solve8x8(A,b,x))return false;
    for(int j=0;j<8;j++)H[j]=x[j]; H[8]=1; return true;
}

// ── Geometry helpers ──────────────────────────────────────────────────

static float Dot2(Vector2 a, Vector2 b) { return a.x*b.x+a.y*b.y; }
static Vector2 Sub2(Vector2 a, Vector2 b) { return {a.x-b.x,a.y-b.y}; }
static Vector2 Add2(Vector2 a, Vector2 b) { return {a.x+b.x,a.y+b.y}; }
static Vector2 Mul2(Vector2 a, float s) { return {a.x*s,a.y*s}; }

static void CornersBbox(const Vector2 c[4], float* ox,float* oy,float* bw,float* bh) {
    float mnx=c[0].x,mxx=c[0].x,mny=c[0].y,mxy=c[0].y;
    for(int i=1;i<4;i++){
        if(c[i].x<mnx)mnx=c[i].x;if(c[i].x>mxx)mxx=c[i].x;
        if(c[i].y<mny)mny=c[i].y;if(c[i].y>mxy)mxy=c[i].y;
    }
    *ox=mnx;*oy=mny;*bw=mxx-mnx;*bh=mxy-mny;
}

static void ComputeDestCorners(const Vector2 src[4], Vector2 dst[4], float aspectRatio) {
    float ox,oy,bw,bh; CornersBbox(src,&ox,&oy,&bw,&bh);
    float cx=ox+bw*0.5f, cy=oy+bh*0.5f;
    float outH=bh;
    float outW=bh*aspectRatio;
    dst[0]={cx-outW*0.5f, cy-outH*0.5f};
    dst[1]={cx+outW*0.5f, cy-outH*0.5f};
    dst[2]={cx+outW*0.5f, cy+outH*0.5f};
    dst[3]={cx-outW*0.5f, cy+outH*0.5f};
}

// ── Direction helpers ─────────────────────────────────────────────────

static Vector2 Dir(Vector2 from, Vector2 to) {
    Vector2 d = Sub2(to, from);
    float l = sqrtf(Dot2(d,d));
    if(l<0.0001f)return (Vector2){1,0};
    return Mul2(d, 1.0f/l);
}

// ── Line intersection helper ──────────────────────────────────────────

static Vector2 Intersect2(Vector2 a1, Vector2 a2, Vector2 b1, Vector2 b2) {
    // line A: a1 + t*(a2-a1),  line B: b1 + u*(b2-b1)
    Vector2 da = Sub2(a2,a1), db = Sub2(b2,b1);
    float det = da.x*db.y - da.y*db.x;
    if (fabsf(det) < 0.0001f) return a1;  // parallel → fallback
    float t = ((b1.x-a1.x)*db.y - (b1.y-a1.y)*db.x) / det;
    return Add2(a1, Mul2(da, t));
}

// ── Recompute corners: TL/BR from stored scalars, BL/TR from constraint
//     BL = intersection of edge(TL→L) with edge(B→BR)
//     TR = intersection of edge(TL→T) with edge(R→BR)
//     This ensures every edge passes through its axis endpoint.

static void RecomputeCorners() {
    Vector2 uL = Dir(g_center, g_left);
    Vector2 uR = Dir(g_center, g_right);
    Vector2 uT = Dir(g_center, g_top);
    Vector2 uB = Dir(g_center, g_bottom);
    // TL and BR from stored scalars
    g_corners[0] = Add2(g_center, Add2(Mul2(uL,g_sa), Mul2(uT,g_sb))); // TL
    g_corners[2] = Add2(g_center, Add2(Mul2(uR,g_sc), Mul2(uB,g_sd))); // BR
    // BL from constraint: on line(TL,L) and line(B,BR)
    g_corners[3] = Intersect2(g_corners[0], g_left, g_bottom, g_corners[2]);
    // TR from constraint: on line(TL,T) and line(R,BR)
    g_corners[1] = Intersect2(g_corners[0], g_top, g_right, g_corners[2]);
    g_warpDirty = true;
}

// ── Forward declarations ──────────────────────────────────────────────

static void GetCoeffs(Vector2 pt, Vector2 center, Vector2 axA, Vector2 axB, float& sA, float& sB);

// ── Init scalars from corner/axis positions ──────────────────────────

static void InitScalars() {
    GetCoeffs(g_corners[0], g_center, g_left, g_top, g_sa, g_sb);
    GetCoeffs(g_corners[2], g_center, g_right, g_bottom, g_sc, g_sd);
    RecomputeCorners();
}

// ── Reset ────────────────────────────────────────────────────────────

static void ResetModel() {
    int cw = LayerStack_RenderW(), ch = LayerStack_RenderH();
    if(cw<1||ch<1){cw=512;ch=512;}
    float mx=cw*0.2f,my=ch*0.2f;
    g_center = {cw*0.5f, ch*0.5f};
    g_left   = {mx,      ch*0.5f};
    g_right  = {cw-mx,   ch*0.5f};
    g_top    = {cw*0.5f, my};
    g_bottom = {cw*0.5f, ch-my};
    g_corners[0]={mx,my};                    // TL
    g_corners[1]={(float)cw-mx,my};          // TR
    g_corners[2]={(float)cw-mx,(float)ch-my};// BR
    g_corners[3]={mx,(float)ch-my};          // BL
    InitScalars();
    g_warpValid=true; g_warpDirty=true;
}

// ── Solve 2x2 to get linear combination coefficients ────────────────
// pt = center + sA*uA + sB*uB  where uA = Dir(center, axA), uB = Dir(center, axB)
static void GetCoeffs(Vector2 pt, Vector2 center, Vector2 axA, Vector2 axB, float& sA, float& sB) {
    Vector2 v = Sub2(pt, center);
    Vector2 uA = Dir(center, axA), uB = Dir(center, axB);
    float det = uA.x*uB.y - uA.y*uB.x;
    if (fabsf(det) < 0.0001f) { sA = Dot2(v,uA); sB = Dot2(v,uB); return; }
    sA = (uB.y*v.x - uB.x*v.y) / det;
    sB = (-uA.y*v.x + uA.x*v.y) / det;
}

// ── Project a point onto axis direction, return scalar ───────────────

static float GetScalar(Vector2 pt, Vector2 center, Vector2 axisPt) {
    Vector2 d = Dir(center, axisPt);
    return Dot2(Sub2(pt,center), d);
}

// ── Warp render ───────────────────────────────────────────────────────

static void EnsureShader() {
    if(g_warpShader.id)return;
    char fs[512]; snprintf(fs,sizeof(fs),"%sshaders/warp.fs",GetApplicationDirectory());
    g_warpShader=LoadShaderWithIncludes(0,fs);
    locSrcTex=GetShaderLocation(g_warpShader,"srcTex");
    locSrcSize=GetShaderLocation(g_warpShader,"srcSize");
    locDstSize=GetShaderLocation(g_warpShader,"dstSize");
    locRow0=GetShaderLocation(g_warpShader,"invH_row0");
    locRow1=GetShaderLocation(g_warpShader,"invH_row1");
    locRow2=GetShaderLocation(g_warpShader,"invH_row2");
    if(locSrcTex>=0){int u=1;SetShaderValue(g_warpShader,locSrcTex,&u,SHADER_UNIFORM_INT);}
}

// ── Common warp render helper ─────────────────────────────────────────
// Renders the warp into an RT at the DESTINATION bbox size.
// Returns the RT or {0} on failure. Caller owns the RT.

static RenderTexture2D RenderWarpRT() {
    RenderTexture2D empty = {0};
    if(!g_warpValid)return empty;
    int cw=LayerStack_RenderW(),ch=LayerStack_RenderH();
    if(cw<1||ch<1)return empty;
    RenderTexture2D* comp=ViewportManager_Composite();
    if(!comp||comp->id==0)return empty;

    Vector2 dst[4]; ComputeDestCorners(g_corners,dst,g_aspectRatio);
    float dstBX,dstBY,dstBW,dstBH;
    CornersBbox(dst,&dstBX,&dstBY,&dstBW,&dstBH);
    int dW=(int)(dstBW+0.5f),dH=(int)(dstBH+0.5f);
    if(dW<2)dW=2;if(dH<2)dH=2;

    Vector2 lDst[4]; for(int i=0;i<4;i++){lDst[i].x=dst[i].x-dstBX;lDst[i].y=dst[i].y-dstBY;}
    float H[9]; if(!SolveHomography(g_corners,lDst,H))return empty;
    float invH[9]; if(!InvertMatrix3(H,invH))return empty;

    EnsureShader();
    if(g_whiteTex.id==0){Image w=GenImageColor(2,2,WHITE);g_whiteTex=LoadTextureFromImage(w);UnloadImage(w);}

    RenderTexture2D rt = LoadRenderTexture(dW,dH);
    if(rt.id==0)return empty;

    BeginTextureMode(rt);
    rlSetBlendMode(RL_BLEND_CUSTOM);rlSetBlendFactors(RL_ONE,RL_ZERO,RL_FUNC_ADD);
    ClearBackground((Color){0,0,0,0});
    rlActiveTextureSlot(1);rlEnableTexture(comp->texture.id);rlActiveTextureSlot(0);
    BeginShaderMode(g_warpShader);
    float sS[2]={(float)cw,(float)ch},dS[2]={(float)dW,(float)dH};
    if(locSrcSize>=0)SetShaderValue(g_warpShader,locSrcSize,sS,SHADER_UNIFORM_VEC2);
    if(locDstSize>=0)SetShaderValue(g_warpShader,locDstSize,dS,SHADER_UNIFORM_VEC2);
    if(locRow0>=0)SetShaderValue(g_warpShader,locRow0,&invH[0],SHADER_UNIFORM_VEC3);
    if(locRow1>=0)SetShaderValue(g_warpShader,locRow1,&invH[3],SHADER_UNIFORM_VEC3);
    if(locRow2>=0)SetShaderValue(g_warpShader,locRow2,&invH[6],SHADER_UNIFORM_VEC3);
    DrawTexturePro(g_whiteTex,(Rectangle){0,0,2,2},(Rectangle){0,0,(float)dW,(float)dH},{0,0},0,WHITE);
    EndShaderMode();
    rlActiveTextureSlot(1);rlDisableTexture();rlActiveTextureSlot(0);
    EndTextureMode();rlSetBlendMode(RL_BLEND_ALPHA);

    return rt;
}

static void UpdateWarpPreview() {
    if(!g_warpValid)return;
    // Recreate g_warpRT on each update (size may change with aspect ratio)
    if(g_warpRT.id)UnloadRenderTexture(g_warpRT);
    g_warpRT = RenderWarpRT();
    g_warpW = g_warpRT.id ? (int)g_warpRT.texture.width : 0;
    g_warpH = g_warpRT.id ? (int)g_warpRT.texture.height : 0;
    g_warpDirty = false;
}

// ── Manual GPU readback (avoids raylib LoadImageFromTexture issues) ──
// Creates an RGBA8 Image from a RenderTexture2D's color attachment
// using a temporary FBO + glReadPixels.

static Image ReadRTImage(RenderTexture2D rt) {
    Image img = {0};
    int w = (int)rt.texture.width, h = (int)rt.texture.height;
    if (w < 1 || h < 1) return img;
    // Cap at reasonable size to prevent OOM
    if (w > 8192) w = 8192; if (h > 8192) h = 8192;

    void* pixels = calloc(1, (size_t)w * h * 4);
    if (!pixels) return img;

    rlDrawRenderBatchActive();
    glFinish();

    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, rt.texture.id, 0);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    img.data = pixels;
    img.width = w;
    img.height = h;
    img.mipmaps = 1;
    img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    return img;
}

static void AcceptWarp(AppState* state) {
    if(!g_warpValid)return;
    RenderTexture2D rt = RenderWarpRT();
    if(rt.id==0)return;
    Image img = ReadRTImage(rt);
    UnloadRenderTexture(rt);
    if(!img.data){printf("[WARP] Accept readback failed\n");return;}
    ImageFlipVertical(&img);
    // ViewportManager_CreateLayerFromImage takes ownership of img.data
    // (it calls UnloadImage internally). DO NOT call UnloadImage again.
    int nl=ViewportManager_CreateLayerFromImage(img);
    if(nl>=0){state->activeLayer=nl;layersDirty=true;}
    // img.data was consumed — do not unload here
    HudSetActive(state,HUD_NONE);
}

// ── Hit test ──────────────────────────────────────────────────────────

static int HitTest(Vector2 mp, const Camera2D* cam) {
    Vector2 sp[5]={g_center,g_left,g_right,g_top,g_bottom};
    for(int i=0;i<5;i++){
        Vector2 s=GetWorldToScreen2D(sp[i],*cam);
        float dx=mp.x-s.x,dy=mp.y-s.y;
        if(sqrtf(dx*dx+dy*dy)<18)return i;
    }
    for(int i=0;i<4;i++){
        Vector2 s=GetWorldToScreen2D(g_corners[i],*cam);
        float dx=mp.x-s.x,dy=mp.y-s.y;
        if(sqrtf(dx*dx+dy*dy)<18)return 5+i;
    }
    return -1;
}

// ── Recalc collinear ─────────────────────────────────────────────────

static void RecalcRight() {
    Vector2 dir = Dir(g_center, g_left);
    g_right = Add2(g_center, Mul2(dir, GetScalar(g_right, g_center, g_left)));
}
static void RecalcLeft() {
    Vector2 dir = Dir(g_center, g_right);
    g_left = Add2(g_center, Mul2(dir, GetScalar(g_left, g_center, g_right)));
}
static void RecalcBottom() {
    Vector2 dir = Dir(g_center, g_top);
    g_bottom = Add2(g_center, Mul2(dir, GetScalar(g_bottom, g_center, g_top)));
}
static void RecalcTop() {
    Vector2 dir = Dir(g_center, g_bottom);
    g_top = Add2(g_center, Mul2(dir, GetScalar(g_top, g_center, g_bottom)));
}

// ── Module ────────────────────────────────────────────────────────────

bool WarpHudModule::HandleInput(InputState& input, const DrawRect& rect) {
    if(!ImGui::IsAnyItemActive()&&input.KeyPressed(KEY_FIVE)){
        if(g_activeHud==HUD_WARP)HudSetActive(state,HUD_NONE);
        else{g_needReset=true;HudSetActive(state,HUD_WARP);}
        return true;
    }
    if(g_activeHud!=HUD_WARP)return false;
    if(g_needReset){ResetModel();g_needReset=false;}

    if(input.KeyPressed(KEY_R)){ResetModel();return false;}
    if(input.KeyPressed(KEY_ENTER)){AcceptWarp(state);return false;}
    if(input.KeyPressed(KEY_ESCAPE)){HudSetActive(state,HUD_NONE);return false;}
    if(ImGui::IsAnyItemHovered()){input.mouseCaptured=true;return false;}
    if(IsKeyDown(KEY_SPACE)||GetMouseWheelMove()!=0)return false;
    if(!rect.Contains(input.MousePos()))return false;

    Vector2 mp=input.MousePos(),cv=GetScreenToWorld2D(mp,state->camera);
    bool isDown=IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    g_hoverWhich=(g_dragWhich>=0)?g_dragWhich:HitTest(mp,&state->camera);

    // --- start drag ---
    if(isDown&&g_dragWhich<0&&g_hoverWhich>=0){
        g_dragWhich=g_hoverWhich;
        Vector2*pt=nullptr;
        if(g_dragWhich==0)pt=&g_center;
        else if(g_dragWhich==1)pt=&g_left;
        else if(g_dragWhich==2)pt=&g_right;
        else if(g_dragWhich==3)pt=&g_top;
        else if(g_dragWhich==4)pt=&g_bottom;
        else pt=&g_corners[g_dragWhich-5];
        g_dragOfs={pt->x-cv.x,pt->y-cv.y};
        input.mouseCaptured=true;return false;
    }

    // --- drag move ---
    if(isDown&&g_dragWhich>=0){
        Vector2 np={cv.x+g_dragOfs.x,cv.y+g_dragOfs.y};
        switch(g_dragWhich){
            case 0:{ // center — axes translate with C, corners recompute
                Vector2 d=Sub2(np,g_center);
                g_center=np; g_left=Add2(g_left,d); g_right=Add2(g_right,d);
                g_top=Add2(g_top,d); g_bottom=Add2(g_bottom,d);
                RecomputeCorners(); break;
            }
            case 1:{ // left — update scalar a to keep L on left edge
                g_left=np; RecalcRight();
                g_sa=GetScalar(np,g_center,g_left);
                RecomputeCorners(); break;
            }
            case 2:{ // right — update scalar c
                g_right=np; RecalcLeft();
                g_sc=GetScalar(np,g_center,g_right);
                RecomputeCorners(); break;
            }
            case 3:{ // top — update scalar b
                g_top=np; RecalcBottom();
                g_sb=GetScalar(np,g_center,g_top);
                RecomputeCorners(); break;
            }
            case 4:{ // bottom — update scalar d
                g_bottom=np; RecalcTop();
                g_sd=GetScalar(np,g_center,g_bottom);
                RecomputeCorners(); break;
            }
            case 5:{ // TL — update scalars, constrain TR and BL, preserve BR
                GetCoeffs(np,g_center,g_left,g_top,g_sa,g_sb);
                g_corners[0]=np;
                g_corners[1]=Intersect2(np,g_top,g_right,g_corners[2]); // TR
                g_corners[3]=Intersect2(np,g_left,g_bottom,g_corners[2]); // BL
                g_warpDirty=true; break;
            }
            case 6:{ // TR — update scalars, constrain TL and BR, preserve BL
                GetCoeffs(np,g_center,g_right,g_top,g_sc,g_sb);
                g_corners[1]=np;
                g_corners[0]=Intersect2(np,g_top,g_left,g_corners[3]); // TL
                g_corners[2]=Intersect2(np,g_right,g_bottom,g_corners[3]); // BR
                g_warpDirty=true; break;
            }
            case 7:{ // BR — update scalars, constrain TR and BL, preserve TL
                GetCoeffs(np,g_center,g_right,g_bottom,g_sc,g_sd);
                g_corners[2]=np;
                g_corners[1]=Intersect2(np,g_right,g_top,g_corners[0]); // TR
                g_corners[3]=Intersect2(np,g_bottom,g_left,g_corners[0]); // BL
                g_warpDirty=true; break;
            }
            case 8:{ // BL — update scalars, constrain TL and BR, preserve TR
                GetCoeffs(np,g_center,g_left,g_bottom,g_sa,g_sd);
                g_corners[3]=np;
                g_corners[0]=Intersect2(np,g_left,g_top,g_corners[1]); // TL
                g_corners[2]=Intersect2(np,g_bottom,g_right,g_corners[1]); // BR
                g_warpDirty=true; break;
            }
        }
        input.mouseCaptured=true;return false;
    }

    g_dragWhich=-1; return false;
}

void WarpHudModule::DrawGL(const DrawRect& rect) {
    if(g_activeHud!=HUD_WARP)return;
    if(LayerStack_RenderW()<1||LayerStack_RenderH()<1)return;
    if(g_warpDirty)UpdateWarpPreview();

    Vector2 cS=GetWorldToScreen2D(g_center,state->camera);
    Vector2 lS=GetWorldToScreen2D(g_left,state->camera);
    Vector2 rS=GetWorldToScreen2D(g_right,state->camera);
    Vector2 tS=GetWorldToScreen2D(g_top,state->camera);
    Vector2 bS=GetWorldToScreen2D(g_bottom,state->camera);

    // Axis guide lines
    DrawLineEx(cS,lS,1.5f/state->camera.zoom,(Color){200,200,255,80});
    DrawLineEx(cS,rS,1.5f/state->camera.zoom,(Color){200,200,255,80});
    DrawLineEx(cS,tS,1.5f/state->camera.zoom,(Color){255,200,200,80});
    DrawLineEx(cS,bS,1.5f/state->camera.zoom,(Color){255,200,200,80});

    // Quad edges
    for(int i=0;i<4;i++){
        Vector2 a=GetWorldToScreen2D(g_corners[i],state->camera);
        Vector2 b=GetWorldToScreen2D(g_corners[(i+1)%4],state->camera);
        DrawLineEx(a,b,2.0f/state->camera.zoom,WHITE);
    }

    // Axis endpoint handles
    struct{Vector2 p;int id;Color c;}ax[]={
        {g_center,0,(Color){255,255,100,255}},
        {g_left,1,(Color){100,200,255,255}},
        {g_right,2,(Color){100,200,255,255}},
        {g_top,3,(Color){255,150,100,255}},
        {g_bottom,4,(Color){255,150,100,255}},
    };
    for(int i=0;i<5;i++){
        Vector2 sp=GetWorldToScreen2D(ax[i].p,state->camera);
        float r=fmaxf(5.0f/state->camera.zoom,4.0f);
        Color c=(g_dragWhich==ax[i].id)?YELLOW:(g_hoverWhich==ax[i].id)?LIME:ax[i].c;
        DrawCircleV(sp,r,c);DrawCircleLinesV(sp,r,BLACK);
    }

    // Corner handles
    for(int i=0;i<4;i++){
        Vector2 sp=GetWorldToScreen2D(g_corners[i],state->camera);
        float r=fmaxf(4.0f/state->camera.zoom,3.0f);
        int id=5+i;
        Color c=(g_dragWhich==id)?YELLOW:(g_hoverWhich==id)?LIME:(Color){200,200,200,200};
        DrawCircleV(sp,r,c);DrawCircleLinesV(sp,r,BLACK);
    }
}

void WarpHudModule::DrawGUI(const DrawRect& rect) {
    if(g_activeHud!=HUD_WARP)return;
    ImGui::SetNextWindowPos(ImVec2(rect.x+rect.w-270,rect.y+10),ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(250,0),ImGuiCond_Once);
    ImGui::Begin("Perspective Warp",NULL,ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::TextWrapped("Drag corners to match the warped region.");
    ImGui::Separator();
    ImGui::Text("Output proportions"); ImGui::SameLine();
    DrawSingleSlider("##ar",&g_aspectRatio,0.25f,4.0f,"W:H %.2f");
    if(ImGui::IsItemActive())g_warpDirty=true;
    ImGui::Separator();

    if(g_warpDirty)UpdateWarpPreview();
    float av=ImGui::GetContentRegionAvail().x;
    // Square preview frame — image centered inside with true aspect ratio
    ImVec2 po=ImGui::GetCursorScreenPos();ImDrawList*dl=ImGui::GetWindowDrawList();
    dl->AddRectFilled(po,ImVec2(po.x+av,po.y+av),IM_COL32(50,50,55,255));
    if(g_warpValid&&g_warpRT.id&&g_warpW>0&&g_warpH>0){
        float imgAR=(float)g_warpW/(float)g_warpH;
        float dw=av, dh=av;
        if(imgAR>1.0f){dw=av;dh=av/imgAR;}
        else{dw=av*imgAR;dh=av;}
        float dx=(av-dw)*0.5f, dy=(av-dh)*0.5f;
        dl->AddImage((ImTextureID)(intptr_t)g_warpRT.texture.id,
            ImVec2(po.x+dx,po.y+dy),ImVec2(po.x+dx+dw,po.y+dy+dh),
            ImVec2(0,1),ImVec2(1,0));
    }else{const char*t="No preview";ImVec2 ts=ImGui::CalcTextSize(t);
        dl->AddText(ImVec2(po.x+(av-ts.x)*.5f,po.y+(av-ts.y)*.5f),IM_COL32(130,130,150,180),t);}
    ImGui::Dummy(ImVec2(av,av+4));
    if(ImGui::Button("Accept Warp",ImVec2(ImGui::GetContentRegionAvail().x,28)))AcceptWarp(state);
    ImGui::Spacing();
    if(ImGui::Button("Reset",ImVec2(ImGui::GetContentRegionAvail().x,0)))ResetModel();
    if(g_warpValid)ImGui::TextColored(ImVec4(.3f,.85f,.3f,1),"Ready  [R]eset  [Enter] Accept  [Esc] Cancel");
    else ImGui::TextColored(ImVec4(.9f,.3f,.3f,1),"Invalid shape");
    ImGui::End();
}

void WarpHudModule::OnExit(){g_dragWhich=-1;g_hoverWhich=-1;}
