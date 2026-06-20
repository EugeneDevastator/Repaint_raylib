#include "repaint.h"
#include "xform.h"
#include "layerstack.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float PackedFloat_GetVal(PackedFloat* pf) { return (float)(pf->IntVal + pf->FVal / 255.0f); }
void PackedFloat_SetVal(PackedFloat* pf, double val) { pf->IntVal=(int16_t)floor(val); pf->FVal=(uint8_t)floor((val-floor(val))*255); }

float Dist2D(Vector2 pos1, Vector2 pos2) { float dx=pos1.x-pos2.x,dy=pos1.y-pos2.y; return sqrtf(dx*dx+dy*dy); }

float AtanXY(float x, float y) {
    double ang; int sg=-1; if(y>0)sg=1;
    if(x==0&&y==0)ang=0; else if(x==0)ang=PI/2.0+(sg+1)/2.0*PI;
    else if(x>0&&y<0)ang=2.0*PI+atanf(y/x); else if(x<0)ang=PI+atanf(y/x); else ang=atanf(y/x);
    return (float)(ang-PI);
}

float RngConv(float inval,float inmin,float inmax,float outmin,float outmax) {
    float inrange=inmax-inmin,inrel=(inval-inmin)/inrange; inrel=fmaxf(inrel,0); inrel=fminf(inrel,1);
    return (outmax-outmin)*inrel+outmin;
}

Document Doc_New(int w, int h) {
    Document d;
    d.ppu = 1.0f;
    d.window = RectXform_Center(w*0.5f, h*0.5f, (float)w, (float)h, 0.0f);
    return d;
}

Document Doc_NewPPU(float ppu, float cw, float ch) {
    Document d;
    d.ppu = ppu;
    d.window = RectXform_Center(cw*0.5f, ch*0.5f, cw, ch, 0.0f);
    return d;
}

void ComputeCanvasMatrix(float ppu, const RectXform* rx, int outW, int outH, float mat[6]) {
    float cx = rx->mat[2], cy = rx->mat[5];
    float c = rx->mat[0], s = rx->mat[1]; // cos(rot), sin(rot)
    mat[0] = ppu * c;
    mat[1] = ppu * s;
    mat[2] = -ppu * (cx * c + cy * s) + outW * 0.5f;
    mat[3] = -ppu * s;
    mat[4] = ppu * c;
    mat[5] =  ppu * (cx * s - cy * c) + outH * 0.5f;
}

void ApplyCanvasWindow(Document* doc) {
    float c = doc->window.mat[0], s = doc->window.mat[1];
    float cx = doc->window.mat[2], cy = doc->window.mat[5];
    float ww = doc->window.w, wh = doc->window.h;
    float ppu = doc->ppu;

    // Crop-only transform: position + rotation, no ppu.
    // Maps old-world point to new-world where crop content is centered.
    float C[6] = {
        c, s, -(cx * c + cy * s) + ww * 0.5f,
        -s, c,  cx * s - cy * c + wh * 0.5f
    };
    LayerStack_SetCanvasView(C);
    LayerStack_BakeCanvasWindow(doc);   // bakes C into layers, resets canvasView to identity

    // ppu-only canvasView — no rotation, no window centering
    float ppuCv[6] = {ppu, 0, 0, 0, ppu, 0};
    LayerStack_SetCanvasView(ppuCv);

    // Reset window: no rotation, centered at (ww/2, wh/2), same world-unit extent
    doc->window.mat[0] = 1; doc->window.mat[1] = 0;
    doc->window.mat[2] = ww * 0.5f;
    doc->window.mat[3] = 0; doc->window.mat[4] = 1;
    doc->window.mat[5] = wh * 0.5f;
    doc->window.w = ww; doc->window.h = wh;

    LayerStack_SetRenderWindow((int)(ww * ppu + 0.5f), (int)(wh * ppu + 0.5f));
}
