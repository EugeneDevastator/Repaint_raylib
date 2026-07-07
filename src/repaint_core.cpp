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
    d.window = RectXform_Pivot(0, 0, (float)w, (float)h, 0.0f);
    return d;
}

void ComputeCanvasMatrix(const RectXform* rx, int outW, int outH, float mat[6]) {
    (void)outW; (void)outH;
    float cx = rx->mat[2], cy = rx->mat[5];
    float a = rx->mat[0], b = rx->mat[1];
    float c = rx->mat[3], d = rx->mat[4];
    float det = a * d - b * c;
    if (fabsf(det) < 0.0001f) { Xform_Identity(mat); return; }
    float invDet = 1.0f / det;
    // paint = W^(-1) * (world - P)
    float ia = d * invDet, ib = -b * invDet;
    float ic = -c * invDet, id = a * invDet;
    mat[0] = ia;
    mat[1] = ib;
    mat[2] = -(ia * cx + ib * cy);
    mat[3] = ic;
    mat[4] = id;
    mat[5] = -(ic * cx + id * cy);
}

void ApplyCanvasWindow(Document* doc) {
    float a = doc->window.mat[0], b = doc->window.mat[1];
    float cx = doc->window.mat[2], cy = doc->window.mat[5];
    float c = doc->window.mat[3], d = doc->window.mat[4];
    float ww = doc->window.ww, wh = doc->window.wh;

    // Decompose rotation + translation from the window matrix (remove scale).
    // Layers are only rotated and moved, not rescaled.
    float sx = sqrtf(a*a + c*c);
    float sy = sqrtf(b*b + d*d);
    float C[6];
    if (sx < 0.0001f || sy < 0.0001f) {
        Xform_Identity(C);
    } else {
        // Normalised rotation columns
        float na = a/sx, nb = b/sy;
        float nc = c/sx, nd = d/sy;
        // Inverse of rotation-only matrix = transpose (orthogonal rotation)
        C[0] = na;  C[1] = nc;  C[2] = -(na*cx + nc*cy);
        C[3] = nb;  C[4] = nd;  C[5] = -(nb*cx + nd*cy);
    }
    LayerStack_SetCanvasView(C);
    LayerStack_BakeCanvasWindow(doc);   // bakes C into layers, resets canvasView to identity

    // Identity canvasView — camera at (0,0) aligns with shifted layer pivot.
    float idCv[6] = {1, 0, 0, 0, 1, 0};
    LayerStack_SetCanvasView(idCv);

    // Reset window: no rotation, pivot at origin, same pixel extent.
    // Texture size (LayerStack_RenderW/H) is NOT changed here — the
    // viewport clips the composite source to min(texSize, ww/wh) so any
    // extra texture area beyond the crop rect is never shown on screen.
    doc->window.mat[0] = 1; doc->window.mat[1] = 0;
    doc->window.mat[2] = 0;
    doc->window.mat[3] = 0; doc->window.mat[4] = 1;
    doc->window.mat[5] = 0;
    doc->window.ww = ww; doc->window.wh = wh;
}
