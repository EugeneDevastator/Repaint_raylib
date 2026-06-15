#include "repaint.h"
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

Document Doc_New(int w, int h) { Document d; d.width=w; d.height=h; return d; }
