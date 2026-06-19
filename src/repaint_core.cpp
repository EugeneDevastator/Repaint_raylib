#include "repaint.h"
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
    d.window.cx = w * 0.5f;
    d.window.cy = h * 0.5f;
    d.window.w = (float)w;
    d.window.h = (float)h;
    d.window.rotation = 0.0f;
    return d;
}

Document Doc_NewPPU(float ppu, float cw, float ch) {
    Document d;
    d.ppu = ppu;
    d.window.cx = cw * 0.5f;
    d.window.cy = ch * 0.5f;
    d.window.w = cw;
    d.window.h = ch;
    d.window.rotation = 0.0f;
    return d;
}

void ComputeCanvasMatrix(float ppu, const CanvasWindow* cw, int outW, int outH, float mat[6]) {
    float c = cosf(cw->rotation), s = sinf(cw->rotation);
    mat[0] = ppu * c;
    mat[1] = ppu * s;
    mat[2] = -ppu * (cw->cx * c + cw->cy * s) + outW * 0.5f;
    mat[3] = -ppu * s;
    mat[4] = ppu * c;
    mat[5] =  ppu * (cw->cx * s - cw->cy * c) + outH * 0.5f;
}

void ApplyCanvasWindow(Document* doc) {
    // Bake the canvas-window transform into every visible layer,
    // then reset the window to identity.
    LayerStack_BakeCanvasWindow(doc);
    // Reset document: identity window at the new output size
    int outW = DocOutW(doc), outH = DocOutH(doc);
    doc->window.cx = outW * 0.5f;
    doc->window.cy = outH * 0.5f;
    doc->window.w = (float)outW;
    doc->window.h = (float)outH;
    doc->window.rotation = 0.0f;
    // ppu unchanged
}
