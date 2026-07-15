#include "repaint.h"
#include "xform.h"
#include "layerstack.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

float PackedFloat_GetVal(PackedFloat* pf) { return (float)(pf->IntVal + pf->FVal / 255.0f); }
void PackedFloat_SetVal(PackedFloat* pf, double val) { pf->IntVal=(int16_t)floor(val); pf->FVal=(uint8_t)floor((val-floor(val))*255); }

float Dist2D(Vector2 pos1, Vector2 pos2) { float dx=pos1.x-pos2.x,dy=pos1.y-pos2.y; return sqrtf(dx*dx+dy*dy); }

float DirAng(float x, float y) {
    return atan2f(y, x);
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
    float cx = rx->mat[2], cy = rx->mat[5];
    float a = rx->mat[0], b = rx->mat[1];
    float c = rx->mat[3], d = rx->mat[4];
    float det = a * d - b * c;
    if (fabsf(det) < 0.0001f) { Xform_Identity(mat); return; }
    float invDet = 1.0f / det;
    // paint = W^(-1) * (world - P)   — maps world to canvas-local space
    float ia = d * invDet, ib = -b * invDet;
    float ic = -c * invDet, id = a * invDet;
    float tx = -(ia * cx + ib * cy);
    float ty = -(ic * cx + id * cy);
    // Scale from canvas-local to texture pixels so the entire world region
    // (ww,wh) maps to the full output texture (outW,outH).
    float sx = (rx->ww > 0.0f) ? (float)outW / rx->ww : 1.0f;
    float sy = (rx->wh > 0.0f) ? (float)outH / rx->wh : 1.0f;
    mat[0] = ia * sx; mat[1] = ib * sx; mat[2] = tx * sx;
    mat[3] = ic * sy; mat[4] = id * sy; mat[5] = ty * sy;
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
    // Texture size stays independent — canvasView will scale so the
    // entire world region (ww,wh) maps to the full canvas RT pixels.
    doc->window.mat[0] = 1; doc->window.mat[1] = 0;
    doc->window.mat[2] = 0;
    doc->window.mat[3] = 0; doc->window.mat[4] = 1;
    doc->window.mat[5] = 0;
    doc->window.ww = ww; doc->window.wh = wh;

    // Recompute canvasView with the reset window and current canvas RT size
    int cw = LayerStack_RenderW(), ch = LayerStack_RenderH();
    if (cw > 0 && ch > 0) {
        float cv[6];
        ComputeCanvasMatrix(&doc->window, cw, ch, cv);
        LayerStack_SetCanvasView(cv);
    }
    LayerStack_SetCanvasXform(&doc->window);
}
