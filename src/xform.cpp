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
