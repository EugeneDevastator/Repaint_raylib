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
    bbr.rad_in    = Stroke_Lerp(from.rad_in, to.rad_in, k);
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

    return bbr;
}

int Stroke_UnpackSection(
    d_Section* section,
    Vector2* out_positions,
    d_Brush* out_brushes,
    int max_dabs
) {
    if (max_dabs <= 0) return 0;

    float stdist = Dist2D(section->Stroke.pos1, section->Stroke.pos2);
    if (stdist < 0.001f) return 0;

    // Radii at each end of the segment (already include SizeMul)
    float rad      = section->BrushFrom.Realb.rad_out;
    float endradius = section->Brush.Realb.rad_out;

    // Direction unit vector (pos1 -> pos2)
    float dx = section->Stroke.pos1.x - section->Stroke.pos2.x;
    float dy = section->Stroke.pos1.y - section->Stroke.pos2.y;
    float x2r = dx / stdist;
    float y2r = dy / stdist;

    // Walk along stroke segment
    float curlen  = 0.0f;
    float nextrad = rad + (curlen * (endradius - rad) / stdist);
    float nextlen = curlen + nextrad * fmaxf(section->spacing, 0.01f);

    // Scatter range: normalized scatter / 51.0 gives 0-5 range from uint8
    float rrang = section->Brush.Realb.rad_out * (section->scatter / 51.0f);

    uint16_t n = 0;
    int count = 0;

    if (nextlen < stdist) {
        while (nextlen < stdist && count < max_dabs) {
            n++;

            // Scatter jitter (perpendicular to stroke direction)
            float rnflw  = Stroke_RawRnd(section->BrushFrom.Realb.seed + n * 2, 1024) * rrang * 2.0f - rrang;
            float rnside = 0.0f;

            Vector2 dotpos1;
            dotpos1.x = section->Stroke.pos2.x + ((nextlen * dx) / stdist) - rnflw * y2r + rnside * x2r;
            dotpos1.y = section->Stroke.pos2.y + ((nextlen * dy) / stdist) + rnflw * x2r + rnside * y2r;

            // Interpolated brush at this position
            float k = nextlen / stdist;
            d_Brush cbrush = section->BrushFrom;
            cbrush.Realb = Stroke_BlendBrushes(section->BrushFrom.Realb, section->Brush.Realb, k);

            // Noise seeding
            if (section->Noisemode == 0) {
                cbrush.Realb.noisex = (uint16_t)(Stroke_RawRnd(section->BrushFrom.Realb.seed + n * 3, 1024) * 1024.0f);
                cbrush.Realb.noisey = (uint16_t)(Stroke_RawRnd(section->BrushFrom.Realb.seed + n + 21, 1024) * 1024.0f);
            } else if (section->Noisemode == 1) {
                cbrush.Realb.noisex = 34;
                cbrush.Realb.noisey = 76;
            } else if (section->Noisemode == 2) {
                cbrush.Realb.noisex = (uint16_t)fmaxf(0, (int)dotpos1.x);
                cbrush.Realb.noisey = (uint16_t)fmaxf(0, (int)dotpos1.y);
            }

            // Keep noise values in [0, 1024)
            cbrush.Realb.noisex = (uint16_t)(cbrush.Realb.noisex - 1024 * (int)(cbrush.Realb.noisex / 1024));
            cbrush.Realb.noisey = (uint16_t)(cbrush.Realb.noisey - 1024 * (int)(cbrush.Realb.noisey / 1024));

            out_positions[count] = dotpos1;
            out_brushes[count]   = cbrush;
            count++;

            nextlen = nextlen + nextrad * fmaxf(section->spacing, 0.01f);
        }
    }

    return count;
}
