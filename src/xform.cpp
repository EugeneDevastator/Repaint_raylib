#include "xform.h"
#include <math.h>
#include <string.h>

void Xform_Mul(float out[6], const float a[6], const float b[6]) {
    out[0] = a[0]*b[0] + a[1]*b[3];
    out[1] = a[0]*b[1] + a[1]*b[4];
    out[2] = a[0]*b[2] + a[1]*b[5] + a[2];
    out[3] = a[3]*b[0] + a[4]*b[3];
    out[4] = a[3]*b[1] + a[4]*b[4];
    out[5] = a[3]*b[2] + a[4]*b[5] + a[5];
}

void Xform_MulInv(float out[6], const float a[6], const float b[6]) {
    float det = b[0]*b[4] - b[1]*b[3];
    if(fabsf(det) < 0.0001f){ memcpy(out, a, 6*sizeof(float)); return; }
    float id = 1.0f/det;
    float ib0 =  b[4]*id, ib1 = -b[1]*id, ibtx = (b[1]*b[5] - b[4]*b[2])*id;
    float ib3 = -b[3]*id, ib4 =  b[0]*id, ibty = (b[3]*b[2] - b[0]*b[5])*id;
    out[0] = a[0]*ib0 + a[1]*ib3;
    out[1] = a[0]*ib1 + a[1]*ib4;
    out[2] = a[0]*ibtx + a[1]*ibty + a[2];
    out[3] = a[3]*ib0 + a[4]*ib3;
    out[4] = a[3]*ib1 + a[4]*ib4;
    out[5] = a[3]*ibtx + a[4]*ibty + a[5];
}

void Xform_Identity(float out[6]) {
    out[0]=1; out[1]=0; out[2]=0;
    out[3]=0; out[4]=1; out[5]=0;
}

void Xform_SetTrans(float out[6], float tx, float ty) {
    out[0]=1; out[1]=0; out[2]=tx;
    out[3]=0; out[4]=1; out[5]=ty;
}

void Xform_SetRot(float out[6], float angle) {
    float c = cosf(angle), s = sinf(angle);
    out[0]=c; out[1]=s; out[2]=0;
    out[3]=-s; out[4]=c; out[5]=0;
}

void Xform_SetScale(float out[6], float sx, float sy) {
    out[0]=sx; out[1]=0; out[2]=0;
    out[3]=0;  out[4]=sy; out[5]=0;
}

RectXform RectXform_Pivot(float cx, float cy, float w, float h, float rot) {
    RectXform rx;
    Xform_SetRot(rx.mat, rot);
    rx.mat[2] = cx;
    rx.mat[5] = cy;
    rx.w = w;
    rx.h = h;
    return rx;
}

float RectXform_GetRot(const RectXform* rx) {
    return atan2f(rx->mat[1], rx->mat[0]);
}

Rectangle GetWorldAABB(const RectXform* rx) {
    float a = rx->mat[0], b = rx->mat[1], tx = rx->mat[2];
    float c = rx->mat[3], d = rx->mat[4], ty = rx->mat[5];
    float w = rx->w, h = rx->h;
    float x0 = tx,           y0 = ty;
    float x1 = a*w + tx,     y1 = c*w + ty;
    float x2 = b*h + tx,     y2 = d*h + ty;
    float x3 = a*w + b*h + tx, y3 = c*w + d*h + ty;
    float l = fminf(fminf(x0,x1), fminf(x2,x3));
    float r = fmaxf(fmaxf(x0,x1), fmaxf(x2,x3));
    float t = fminf(fminf(y0,y1), fminf(y2,y3));
    float bt = fmaxf(fmaxf(y0,y1), fmaxf(y2,y3));
    return Rectangle{l, t, r-l, bt-t};
}

bool GetWorldIntersectionAABB(const RectXform* a, const RectXform* b, Rectangle* out) {
    Rectangle aa = GetWorldAABB(a);
    Rectangle bb = GetWorldAABB(b);
    float l = fmaxf(aa.x, bb.x);
    float t = fmaxf(aa.y, bb.y);
    float r = fminf(aa.x + aa.width, bb.x + bb.width);
    float bt = fminf(aa.y + aa.height, bb.y + bb.height);
    if (l < r && t < bt) {
        if (out) { out->x = l; out->y = t; out->width = r-l; out->height = bt-t; }
        return true;
    }
    return false;
}
