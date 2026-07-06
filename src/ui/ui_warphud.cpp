#include "repaint.h"
#include "RaylibUtils.h"
#include "layerstack.h"
#include "viewport_manager.h"
#include "render_utils.h"
#include "rlgl.h"
#include "imgui.h"
#include "external/glad.h"
#include <math.h>

extern bool layersDirty;

// ── Model ─────────────────────────────────────────────────────────────
// 5-point perspective grid:
//   1. Center (C) — maps to output texture center
//   2. L/R (left/right midpoints, collinear with C) — map to output
//      left/right side midpoints
//   3. T/B (top/bottom midpoints, collinear with C) — map to output
//      top/bottom side midpoints
//
// Corners are computed from a DLT-solved homography that maps a local
// unit frame (C=0,0, L=(-1,0), R=(1,0), T=(0,-1), B=(0,1)) to the
// user's 5 screen points, then evaluated at (-1,-1),(1,-1),(1,1),(-1,1).
//
// Axis drags → recompute corners from the 5-point perspective model.
// Corner drags → store raw position; next axis drag recomputes it.

static Vector2 g_center  = {0,0};
static Vector2 g_left    = {0,0};
static Vector2 g_right   = {0,0};
static Vector2 g_top     = {0,0};
static Vector2 g_bottom  = {0,0};

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

// ── Perspective corner computation from 5-point grid ─────────────────
// Local unit frame:  C=(0,0)  L=(-1,0)  R=(1,0)  T=(0,-1)  B=(0,1)
// A homography H maps local→screen. H is solved from the 5 correspondences
// using DLT (h13=cx, h23=cy from C; h31,h32 averaged from L/R/T/B).
// The 4 corners are H(-1,-1), H(1,-1), H(1,1), H(-1,1).
// This guarantees C→texture center and midpoints→side midpoints.

static void RecomputeCorners() {
    float cx=g_center.x, cy=g_center.y;
    float lx=g_left.x, ly=g_left.y, rx=g_right.x, ry=g_right.y;
    float tx=g_top.x, ty=g_top.y, bx=g_bottom.x, by=g_bottom.y;

    float h31=0, h32=0;
    // h31 from L/R (x and y constraints, averaged)
    float drx = rx - lx, dry = ry - ly;
    if (fabsf(drx) > 0.0001f && fabsf(dry) > 0.0001f)
        h31 = ((2*cx-rx-lx)/drx + (2*cy-ry-ly)/dry) * 0.5f;
    // h32 from T/B
    float dtbx = tx - bx, dtby = ty - by;
    if (fabsf(dtbx) > 0.0001f && fabsf(dtby) > 0.0001f)
        h32 = ((bx+tx-2*cx)/dtbx + (by+ty-2*cy)/dtby) * 0.5f;

    float h11 = cx - lx + h31*lx;
    float h12 = cx - tx + h32*tx;
    float h13 = cx;
    float h21 = cy - ly + h31*ly;
    float h22 = cy - ty + h32*ty;
    float h23 = cy;

    auto app = [&](float u, float v)->Vector2{
        float w = h31*u + h32*v + 1;
        if(fabsf(w)<0.0001f)return g_center;
        return {(h11*u+h12*v+h13)/w, (h21*u+h22*v+h23)/w};
    };

    g_corners[0] = app(-1,-1); // TL
    g_corners[1] = app( 1,-1); // TR
    g_corners[2] = app( 1, 1); // BR
    g_corners[3] = app(-1, 1); // BL
    g_warpDirty = true;
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
    RecomputeCorners();
    g_warpValid=true; g_warpDirty=true;
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
            case 1:{ // left — rotate axis, recompute corners
                g_left=np; RecalcRight(); RecomputeCorners(); break;
            }
            case 2:{ // right
                g_right=np; RecalcLeft(); RecomputeCorners(); break;
            }
            case 3:{ // top
                g_top=np; RecalcBottom(); RecomputeCorners(); break;
            }
            case 4:{ // bottom
                g_bottom=np; RecalcTop(); RecomputeCorners(); break;
            }
            case 5: g_corners[0]=np; g_warpDirty=true; break; // TL
            case 6: g_corners[1]=np; g_warpDirty=true; break; // TR
            case 7: g_corners[2]=np; g_warpDirty=true; break; // BR
            case 8: g_corners[3]=np; g_warpDirty=true; break; // BL
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
