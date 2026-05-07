#include "repaint.h"
#include "rlgl.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#define CLAMP(v, lo, hi) fmaxf((lo), fminf((hi), (v)))
#define FLERPD(v, lo, hi) ((float)(v) / 255.0f)
#define FROUND(v) ((unsigned char)(CLAMP(v, 0.0f, 1.0f) * 255.0f + 0.5f))
#define FLERP(a, b, t) ((a) + ((b) - (a)) * (t))
#define FPIX(data, w, x, y) ((Color*)data)[(y) * (w) + (x)]

static float crPinch[256];
static float crBub[256];
static float crCont[256][256];
static int crClampTbl[256];
static bool painterInited = false;

static Shader brushGenShader = {0};
static Shader brushRenderShader = {0};
static Texture2D noiseTexture = {0};

void Painter_Init(void) {
    if (painterInited) return;
    srand((unsigned int)time(NULL));

    for (int j = 0; j <= 255; j++) {
        float sal = (float)j / 255.0f;
        sal = sal * sal * sal;
        crPinch[j] = sal;
        sal = (float)j / 255.0f;
        sal = 1.0f + (sal - 1.0f) * (sal - 1.0f) * (sal - 1.0f);
        crBub[j] = sal;
    }

    for (int m = 0; m < 256; m++) {
        for (int j = 0; j <= m; j++) {
            float sal = (float)j / (float)(m > 0 ? m : 1);
            sal = sal * sal * sal;
            crCont[m][j] = sal * m / 255.0f;
        }
        for (int j = m + 1; j < 256; j++) {
            float sal = (float)(j - (m + 1)) / (float)(255 - m > 0 ? 255 - m : 1);
            sal = 1.0f + (sal - 1.0f) * (sal - 1.0f) * (sal - 1.0f);
            crCont[m][j] = (sal * (255 - m) + m + 1) / 255.0f;
        }
    }

    for (int i = 0; i < 256; i++) {
        float top = (float)i / 255.0f;
        int imid = (int)((1.0f - top) * 255.0f);
        crClampTbl[i] = imid;
    }

    // Load brush shaders
    brushGenShader = LoadShader("shaders/brush_gen.vs", "shaders/brush_gen.fs");
    brushRenderShader = LoadShader("shaders/brush_gen.vs", "shaders/brush_render.fs");

    // Generate noise texture for brush_gen.fs
    Image noiseImg = GenImageColor(128, 128, BLANK);
    for (int y = 0; y < 128; y++)
        for (int x = 0; x < 128; x++)
            ((Color*)noiseImg.data)[y * 128 + x] = (Color){
                (unsigned char)(rand() % 256),
                (unsigned char)(rand() % 256),
                (unsigned char)(rand() % 256),
                255
            };
    noiseTexture = LoadTextureFromImage(noiseImg);
    UnloadImage(noiseImg);

    painterInited = true;
}

void Painter_Shutdown(void) {
    if (!painterInited) return;
    UnloadShader(brushGenShader);
    UnloadShader(brushRenderShader);
    UnloadTexture(noiseTexture);
    painterInited = false;
}

static inline float lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static inline float clamp01(float v) {
    return fmaxf(0.0f, fminf(1.0f, v));
}

static float randf(void) {
    return (float)rand() / (float)RAND_MAX;
}

static inline float softlight(float d, float s) {
    if (s <= 0.5f) return d - (1.0f - 2.0f * s) * d * (1.0f - d);
    float d2 = d * d;
    if (d <= 0.25f) return d + (2.0f * s - 1.0f) * ((16.0f * d2 - 12.0f) * d + 3.0f) / 12.0f;
    return d + (2.0f * s - 1.0f) * sqrtf(d);
}

Color BlendColors(Color dst_raw, Color src_raw, int blendMode) {
    float dr = dst_raw.r / 255.0f, dg = dst_raw.g / 255.0f, db = dst_raw.b / 255.0f, da = dst_raw.a / 255.0f;
    float sr = src_raw.r / 255.0f, sg = src_raw.g / 255.0f, sb = src_raw.b / 255.0f, sa = src_raw.a / 255.0f;
    float cr, cg, cb;

    if (sa <= 0.0f) return dst_raw;

    switch (blendMode) {
        case bmNormal:   cr = sr; cg = sg; cb = sb; break;
        case bmPlus:     cr = dr + sr; cg = dg + sg; cb = db + sb; break;
        case bmDodge:
            cr = (sr >= 1.0f) ? 1.0f : fminf(dr / (1.0f - sr + 0.0001f), 1.0f);
            cg = (sg >= 1.0f) ? 1.0f : fminf(dg / (1.0f - sg + 0.0001f), 1.0f);
            cb = (sb >= 1.0f) ? 1.0f : fminf(db / (1.0f - sb + 0.0001f), 1.0f);
            break;
        case bmScreen:   cr = dr + sr - dr * sr; cg = dg + sg - dg * sg; cb = db + sb - db * sb; break;
        case bmLighten:  cr = fmaxf(dr, sr); cg = fmaxf(dg, sg); cb = fmaxf(db, sb); break;
        case bmBurn:
            cr = (sr <= 0.0f) ? 0.0f : 1.0f - fminf((1.0f - dr) / (sr + 0.0001f), 1.0f);
            cg = (sg <= 0.0f) ? 0.0f : 1.0f - fminf((1.0f - dg) / (sg + 0.0001f), 1.0f);
            cb = (sb <= 0.0f) ? 0.0f : 1.0f - fminf((1.0f - db) / (sb + 0.0001f), 1.0f);
            break;
        case bmMult:     cr = dr * sr; cg = dg * sg; cb = db * sb; break;
        case bmDarken:   cr = fminf(dr, sr); cg = fminf(dg, sg); cb = fminf(db, sb); break;
        case bmOvr:
            cr = (dr < 0.5f) ? 2.0f * dr * sr : 1.0f - 2.0f * (1.0f - dr) * (1.0f - sr);
            cg = (dg < 0.5f) ? 2.0f * dg * sg : 1.0f - 2.0f * (1.0f - dg) * (1.0f - sg);
            cb = (db < 0.5f) ? 2.0f * db * sb : 1.0f - 2.0f * (1.0f - db) * (1.0f - sb);
            break;
        case bmHlight:
            cr = (sr < 0.5f) ? 2.0f * dr * sr : 1.0f - 2.0f * (1.0f - dr) * (1.0f - sr);
            cg = (sg < 0.5f) ? 2.0f * dg * sg : 1.0f - 2.0f * (1.0f - dg) * (1.0f - sg);
            cb = (sb < 0.5f) ? 2.0f * db * sb : 1.0f - 2.0f * (1.0f - db) * (1.0f - sb);
            break;
        case bmSlight:
            cr = softlight(dr, sr); cg = softlight(dg, sg); cb = softlight(db, sb);
            break;
        case bmXor:
            cr = dr * (1.0f - sa) + sr * (1.0f - da);
            cg = dg * (1.0f - sa) + sg * (1.0f - da);
            cb = db * (1.0f - sa) + sb * (1.0f - da);
            break;
        case bmDiff:     cr = fabsf(dr - sr); cg = fabsf(dg - sg); cb = fabsf(db - sb); break;
        case bmExc:      cr = dr + sr - 2.0f * dr * sr; cg = dg + sg - 2.0f * dg * sg; cb = db + sb - 2.0f * db * sb; break;
        default:         cr = sr; cg = sg; cb = sb; break;
    }

    float out_a = sa + da * (1.0f - sa);
    float out_r = cr * sa + dr * (1.0f - sa);
    float out_g = cg * sa + dg * (1.0f - sa);
    float out_b = cb * sa + db * (1.0f - sa);

    return (Color){
        FROUND(out_r), FROUND(out_g), FROUND(out_b), FROUND(out_a)
    };
}

float GenCurveF(float val, float midp) {
    if (midp < 0) {
        float nmid = 1.0f - (-midp);
        float fpos = 1.0f + powf(val - 1.0f, 3.0f);
        return (fpos - val) * nmid + val;
    } else if (midp > 0) {
        return (val * val * val - val) * midp + val;
    }
    return val;
}

void GenClamp(Image* img, float top, float min) {
    if (min >= top) min = top - 0.1f;
    int wd = img->width;
    int imid = (int)((1.0f - top) * 255.0f);
    float mul = 1.0f / (top - min > 0.001f ? top - min : 0.001f);

    for (int y = 0; y < wd; y++) {
        Color* px = &FPIX(img->data, wd, 0, y);
        for (int x = 0; x < wd; x++) {
            int sal = px[x].a;
            int tsal = (int)((sal / 255.0f - min) * mul * 255.0f);
            tsal = (tsal < 0) ? 0 : (tsal > 255) ? 255 : tsal;
            int idx = imid + tsal;
            idx = (idx < 0) ? 0 : (idx > 255) ? 255 : idx;
            float cv = crCont[imid][idx];
            px[x].a = FROUND(cv);
        }
    }
}

void GenFocal(Image* img, float fop) {
    int wd = img->width;
    for (int y = 0; y < wd; y++) {
        Color* px = &FPIX(img->data, wd, 0, y);
        for (int x = 0; x < wd; x++) {
            int sal = px[x].a;
            float bpn = crPinch[sal];
            sal = (int)(((bpn - (float)sal / 255.0f) * fop + (float)sal / 255.0f) * 255.0f);
            px[x].a = (unsigned char)CLAMP(sal, 0, 255);
        }
    }
}

void GenFocalInv(Image* img, float fop) {
    int wd = img->width;
    for (int y = 0; y < wd; y++) {
        Color* px = &FPIX(img->data, wd, 0, y);
        for (int x = 0; x < wd; x++) {
            int sal = px[x].a;
            float bpn = crBub[sal];
            sal = (int)(((bpn - (float)sal / 255.0f) * fop + (float)sal / 255.0f) * 255.0f);
            px[x].a = (unsigned char)CLAMP(sal, 0, 255);
        }
    }
}

void GenSolidityP(Image* img, float sol, float sol2op, int16_t noisex, int16_t noisey) {
    int wd = img->width;
    sol = CLAMP(sol, 0.0f, 1.0f);
    float fsol2op = CLAMP(1.0f - sol2op, 0.0f, 1.0f);

    for (int y = 0; y < wd; y++) {
        Color* px = &FPIX(img->data, wd, 0, y);
        for (int x = 0; x < wd; x++) {
            float noise_val = randf();
            int sal = px[x].a;
            int nsal = (int)(sal * ((noise_val < sol) ? 1 : 0));
            float noiseres = (noise_val < (float)sal / 255.0f) ? 1.0f : 0.0f;
            nsal = (int)(noiseres * 255.0f * fsol2op + (float)nsal * (1.0f - fsol2op));
            px[x].a = (unsigned char)CLAMP(nsal, 0, 255);
        }
    }
}

static Image GenBMask_CPU(d_Brush* brush, float fx, float fy) {
    int wd = (int)ceilf(brush->Realb.rad_out * 2.0f);
    if (wd < 2) wd = 2;

    Image img = GenImageColor(wd, wd, (Color){0, 0, 0, 0});
    float cx = (float)wd / 2.0f;
    float cy = (float)wd / 2.0f;
    float rad_out = brush->Realb.rad_out;
    float rad_in = brush->Realb.rad_in;
    Color col = brush->Realb.col;

    for (int y = 0; y < wd; y++) {
        Color* px = &FPIX(img.data, wd, 0, y);
        for (int x = 0; x < wd; x++) {
            float dx = (float)x - cx + fx;
            float dy = (float)y - cy + fy;
            float dist = sqrtf(dx * dx + dy * dy);

            if (dist > rad_out) continue;

            float t = dist / rad_out;
            float innerT = rad_in / rad_out;

            float alpha = 1.0f;
            if (t > innerT && rad_out > rad_in) {
                alpha = 1.0f - ((t - innerT) / (1.0f - innerT));
            }
            alpha = CLAMP(alpha, 0.0f, 1.0f);

            float crv = brush->Realb.crv;
            if (crv < 0) {
                float fpos = 1.0f + powf(t - 1.0f, 3.0f);
                alpha = (fpos - t) * (-crv) + t;
            } else if (crv > 0) {
                alpha = (t * t * t - t) * crv + t;
            }
            alpha = CLAMP(alpha, 0.0f, 1.0f);
            px[x] = (Color){255, 255, 255, FROUND(alpha)};
        }
    }

    if (brush->Realb.pipeID == plCFNSR) {
        GenClamp(&img, 1.0f - (brush->Realb.rad_in / brush->Realb.rad_out > 0.001f ?
                               brush->Realb.rad_in / brush->Realb.rad_out : 0.0f), 0.0f);
        float fop = brush->Realb.crv;
        if (fop <= 0) GenFocalInv(&img, fabsf(fop));
        else if (fop <= 1) GenFocal(&img, fop);
        if (!(brush->Realb.sol >= 0.999f && brush->Realb.sol2op <= 0.001f))
            GenSolidityP(&img, brush->Realb.sol, brush->Realb.sol2op,
                         brush->Realb.noisex, brush->Realb.noisey);
    }
    return img;
}

Image Painter_GenBMask(d_Brush* brush, float fx, float fy) {
    // Use CPU brush generation for reliability; GPU shader path available in GenBMask_GPU
    return GenBMask_CPU(brush, fx, fy);
}

void Painter_DrawDab(Image* layer, Vector2 pos, d_Brush* brush, int toolID) {
    float tscale = brush->Realb.scale;
    float rad_out = brush->Realb.rad_out;
    float x2y = brush->Realb.x2y;
    float y2x = 1.0f - (x2y - 0.5f) * 2.0f;
    y2x = fminf(y2x, 1.0f);
    x2y = fminf(x2y * 2.0f, 1.0f);

    float twd = rad_out * tscale;
    if (twd < 1.0f) twd = 1.0f;
    int iwd = (int)ceilf(twd);

    Vector2 fpos = { pos.x - floorf(pos.x), pos.y - floorf(pos.y) };
    Vector2 ipos = { floorf(pos.x) - iwd, floorf(pos.y) - iwd };

    if (toolID == eSmudge || toolID == eDisp || toolID == eCont) {
        int cw = (int)ceilf(twd * 2);
        ipos.x = fmaxf(0, fminf((float)layer->width - cw, ipos.x));
        ipos.y = fmaxf(0, fminf((float)layer->height - cw, ipos.y));
    }

    Image bmask = Painter_GenBMask(brush, fpos.x, fpos.y);
    int mwd = bmask.width;

    int maxSize = (int)ceilf(twd * 2.0f) + 4;
    if (maxSize < mwd) maxSize = mwd;

    int dstX = (int)ipos.x;
    int dstY = (int)ipos.y;
    int dstW = maxSize;
    int dstH = maxSize;

    if (dstX < 0) dstX = 0;
    if (dstY < 0) dstY = 0;
    if (dstX + dstW > layer->width) dstW = layer->width - dstX;
    if (dstY + dstH > layer->height) dstH = layer->height - dstY;
    if (dstW <= 0 || dstH <= 0) { UnloadImage(bmask); return; }

    float op = brush->Realb.opacity;
    uint8_t presop = brush->Realb.preserveop;
    int bmidx = brush->Realb.bmidx;
    Color bcol = brush->Realb.col;

    Color* lpixels = (Color*)layer->data;

    if (toolID == eBrush) {
        for (int y = 0; y < dstH; y++) {
            int ly = dstY + y;
            if (ly >= layer->height) break;
            for (int x = 0; x < dstW; x++) {
                int lx = dstX + x;
                if (lx >= layer->width) break;

                float sx = (float)x - (float)dstW / 2.0f + (twd - rad_out);
                float sy = (float)y - (float)dstH / 2.0f + (twd - rad_out);

                float rx = sx * cosf(-brush->Realb.resangle * PI / 180.0f) - sy * sinf(-brush->Realb.resangle * PI / 180.0f);
                float ry = sx * sinf(-brush->Realb.resangle * PI / 180.0f) + sy * cosf(-brush->Realb.resangle * PI / 180.0f);

                float mx = (rx / (tscale * (x2y > 0.001f ? x2y : 0.001f))) + (float)bmask.width / 2.0f;
                float my = (ry / (tscale * (y2x > 0.001f ? y2x : 0.001f))) + (float)bmask.height / 2.0f;

                if (mx < 0 || mx >= bmask.width || my < 0 || my >= bmask.height) continue;

                int mix = (int)mx;
                int miy = (int)my;
                Color* mpixels = (Color*)bmask.data;
                Color mc = mpixels[miy * bmask.width + mix];
                float srcAlpha = (mc.a / 255.0f) * op;

                if (srcAlpha <= 0.0f) continue;

                Color srcCol = {bcol.r, bcol.g, bcol.b, FROUND(srcAlpha)};
                Color dstCol = lpixels[ly * layer->width + lx];
                Color res = BlendColors(dstCol, srcCol, bmidx);

                if (presop) res.a = dstCol.a;

                lpixels[ly * layer->width + lx] = res;
            }
        }
    } else if (toolID == eSmudge) {
        float cop = brush->Realb.cop;
        for (int y = 0; y < dstH; y++) {
            int ly = dstY + y;
            if (ly >= layer->height) break;
            for (int x = 0; x < dstW; x++) {
                int lx = dstX + x;
                if (lx >= layer->width) break;

                float sx = (float)x - (float)dstW / 2.0f + (twd - rad_out);
                float sy = (float)y - (float)dstH / 2.0f + (twd - rad_out);
                float rx = sx * cosf(-brush->Realb.resangle * PI / 180.0f) - sy * sinf(-brush->Realb.resangle * PI / 180.0f);
                float ry = sx * sinf(-brush->Realb.resangle * PI / 180.0f) + sy * cosf(-brush->Realb.resangle * PI / 180.0f);
                float mx = (rx / (tscale * (x2y > 0.001f ? x2y : 0.001f))) + (float)bmask.width / 2.0f;
                float my = (ry / (tscale * (y2x > 0.001f ? y2x : 0.001f))) + (float)bmask.height / 2.0f;

                if (mx < 0 || mx >= bmask.width || my < 0 || my >= bmask.height) continue;

                int mix = (int)mx, miy = (int)my;
                Color* mpixels = (Color*)bmask.data;
                Color mc = mpixels[miy * bmask.width + mix];
                float maskAlpha = (mc.a / 255.0f) * op;

                if (maskAlpha <= 0.0f) continue;

                Color dstCol = lpixels[ly * layer->width + lx];

                float srcR = lerp((float)dstCol.r / 255.0f, (float)bcol.r / 255.0f, cop);
                float srcG = lerp((float)dstCol.g / 255.0f, (float)bcol.g / 255.0f, cop);
                float srcB = lerp((float)dstCol.b / 255.0f, (float)bcol.b / 255.0f, cop);

                Color srcCol = {FROUND(srcR), FROUND(srcG), FROUND(srcB), FROUND(maskAlpha)};
                Color res = BlendColors(dstCol, srcCol, bmNormal);
                if (presop) res.a = dstCol.a;
                lpixels[ly * layer->width + lx] = res;
            }
        }
    } else if (toolID == eDisp) {
        float pwr = brush->Realb.pwr * 2.0f;
        for (int y = 0; y < dstH; y++) {
            int ly = dstY + y;
            if (ly >= layer->height) break;
            for (int x = 0; x < dstW; x++) {
                int lx = dstX + x;
                if (lx >= layer->width) break;

                float sx = (float)x - (float)dstW / 2.0f + (twd - rad_out);
                float sy = (float)y - (float)dstH / 2.0f + (twd - rad_out);
                float rx = sx * cosf(-brush->Realb.resangle * PI / 180.0f) - sy * sinf(-brush->Realb.resangle * PI / 180.0f);
                float ry = sx * sinf(-brush->Realb.resangle * PI / 180.0f) + sy * cosf(-brush->Realb.resangle * PI / 180.0f);
                float mx = (rx / (tscale * (x2y > 0.001f ? x2y : 0.001f))) + (float)bmask.width / 2.0f;
                float my = (ry / (tscale * (y2x > 0.001f ? y2x : 0.001f))) + (float)bmask.height / 2.0f;

                if (mx < 0 || mx >= bmask.width || my < 0 || my >= bmask.height) continue;

                int mix = (int)mx, miy = (int)my;
                Color* mpixels = (Color*)bmask.data;
                Color mc = mpixels[miy * bmask.width + mix];
                float maskAlpha = (mc.a / 255.0f) * op;

                if (maskAlpha <= 0.0f) continue;

                int sxdisp = (int)((randf() - 0.5f) * pwr * twd * 0.2f);
                int sydisp = (int)((randf() - 0.5f) * pwr * twd * 0.2f);
                int srcX = lx + sxdisp;
                int srcY = ly + sydisp;
                srcX = (int)CLAMP(srcX, 0, layer->width - 1);
                srcY = (int)CLAMP(srcY, 0, layer->height - 1);

                Color srcCol = lpixels[srcY * layer->width + srcX];
                srcCol.a = FROUND(maskAlpha);
                Color dstCol = lpixels[ly * layer->width + lx];
                Color res = BlendColors(dstCol, srcCol, bmNormal);
                if (presop) res.a = dstCol.a;
                lpixels[ly * layer->width + lx] = res;
            }
        }
    } else if (toolID == eCont) {
        for (int y = 0; y < dstH; y++) {
            int ly = dstY + y;
            if (ly >= layer->height) break;
            for (int x = 0; x < dstW; x++) {
                int lx = dstX + x;
                if (lx >= layer->width) break;

                float sx = (float)x - (float)dstW / 2.0f + (twd - rad_out);
                float sy = (float)y - (float)dstH / 2.0f + (twd - rad_out);
                float rx = sx * cosf(-brush->Realb.resangle * PI / 180.0f) - sy * sinf(-brush->Realb.resangle * PI / 180.0f);
                float ry = sx * sinf(-brush->Realb.resangle * PI / 180.0f) + sy * cosf(-brush->Realb.resangle * PI / 180.0f);
                float mx = (rx / (tscale * (x2y > 0.001f ? x2y : 0.001f))) + (float)bmask.width / 2.0f;
                float my = (ry / (tscale * (y2x > 0.001f ? y2x : 0.001f))) + (float)bmask.height / 2.0f;

                if (mx < 0 || mx >= bmask.width || my < 0 || my >= bmask.height) continue;

                int mix = (int)mx, miy = (int)my;
                Color* mpixels = (Color*)bmask.data;
                Color mc = mpixels[miy * bmask.width + mix];
                float maskAlpha = (mc.a / 255.0f) * op;

                if (maskAlpha <= 0.0f) continue;

                Color dstCol = lpixels[ly * layer->width + lx];
                int avg = (dstCol.r + dstCol.g + dstCol.b) / 3;
                int midp = (int)CLAMP(avg, 0, 255);
                float cvr = crCont[midp][dstCol.r];
                float cvg = crCont[midp][dstCol.g];
                float cvb = crCont[midp][dstCol.b];
                Color srcCol = {FROUND(cvr), FROUND(cvg), FROUND(cvb), FROUND(maskAlpha)};
                Color res = BlendColors(dstCol, srcCol, bmNormal);
                if (presop) res.a = dstCol.a;
                lpixels[ly * layer->width + lx] = res;
            }
        }
    }

    UnloadImage(bmask);
}

void Painter_DrawLine(Image* layer, Vector2 from, Vector2 to, d_Brush* brush) {
    float dist = Dist2D(from, to);
    if (dist < 0.5f) {
        Painter_DrawDab(layer, from, brush, eBrush);
        return;
    }

    float spacing = brush->Realb.rad_out * 0.25f;
    if (spacing < 1.0f) spacing = 1.0f;

    int steps = (int)(dist / spacing) + 1;
    if (steps < 2) steps = 2;

    for (int i = 0; i <= steps; i++) {
        float t = (float)i / (float)steps;
        Vector2 pos = {
            from.x + (to.x - from.x) * t,
            from.y + (to.y - from.y) * t
        };
        Painter_DrawDab(layer, pos, brush, eBrush);
    }
}
