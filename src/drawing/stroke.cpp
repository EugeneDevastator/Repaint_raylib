#include "stroke.h"
#include <math.h>
#include <string.h>

float Stroke_RawRnd(uint16_t seed, float range) {
    uint32_t a = 64136401;
    uint32_t m = 25500;
    float x = (float)((a * (uint32_t)seed * (uint32_t)seed * (uint32_t)seed) % m);
    return x / 25500.0f * range;
}

float Stroke_Lerp(float from, float to, float k) {
    return from + (to - from) * k;
}

d_RealBrush Stroke_BlendBrushes(d_RealBrush from, d_RealBrush to, float k) {
    d_RealBrush bbr;
    bbr.radInRatio = Stroke_Lerp(from.radInRatio, to.radInRatio, k);
    bbr.rad_out   = Stroke_Lerp(from.rad_out, to.rad_out, k);
    bbr.crv       = Stroke_Lerp(from.crv, to.crv, k);
    bbr.resangle  = Stroke_Lerp((float)from.resangle, (float)to.resangle, k);
    bbr.pwr       = Stroke_Lerp(from.pwr, to.pwr, k);
    bbr.x2y       = Stroke_Lerp(from.x2y, to.x2y, k);
    bbr.opacity   = Stroke_Lerp(from.opacity, to.opacity, k);
    bbr.cop       = Stroke_Lerp(from.cop, to.cop, k);
    bbr.sol       = Stroke_Lerp(from.sol, to.sol, k);
    bbr.sol2op    = Stroke_Lerp(from.sol2op, to.sol2op, k);

    // Color interpolation
    uint8_t rf = (uint8_t)(from.col.r + (to.col.r - from.col.r) * k);
    uint8_t gf = (uint8_t)(from.col.g + (to.col.g - from.col.g) * k);
    uint8_t bf = (uint8_t)(from.col.b + (to.col.b - from.col.b) * k);
    uint8_t af = (uint8_t)(from.col.a + (to.col.a - from.col.a) * k);
    bbr.col = Color{rf, gf, bf, af};

    // Non-interpolated: carry forward from the first brush
    bbr.seed       = from.seed;
    bbr.NoiseID    = from.NoiseID;
    bbr.MaskID     = from.MaskID;
    bbr.pipeID     = from.pipeID;
    bbr.bmidx      = from.bmidx;
    bbr.noiseidx   = from.noiseidx;
    bbr.preserveop = from.preserveop;
    bbr.texId      = from.texId;
    bbr.texBlendMode = from.texBlendMode;
    bbr.texBlendVal  = from.texBlendVal;
    bbr.texNoisemode = from.texNoisemode;
    bbr.texScale     = from.texScale;
    bbr.texFeather   = from.texFeather;
    bbr.texThresh    = from.texThresh;
    bbr.useTexLumAsAlpha = from.useTexLumAsAlpha;
    bbr.texUseRGB    = from.texUseRGB;
    bbr.texColorMode = from.texColorMode;
    bbr.perspective  = from.perspective;
    bbr.eraseMode    = from.eraseMode;
    bbr.userTexOriginX   = from.userTexOriginX;
    bbr.userTexOriginY   = from.userTexOriginY;
    bbr.userTexDirection = from.userTexDirection;

    return bbr;
}
