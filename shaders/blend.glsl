// blend.glsl — shared GLSL functions for Repaint shaders
// Included via: #include blend.glsl  (at column 0)

// ── OKLab color space ──────────────────────────────────────────────

vec3 rgbToOklab(vec3 c) {
    vec3 lin = c * c;
    float l = 0.4122214708 * lin.r + 0.5363325363 * lin.g + 0.0514459929 * lin.b;
    float m = 0.2119034982 * lin.r + 0.6806995451 * lin.g + 0.1073969566 * lin.b;
    float s = 0.0883024619 * lin.r + 0.2817188376 * lin.g + 0.6299787005 * lin.b;
    float l_ = pow(max(l, 0.0), 1.0/3.0);
    float m_ = pow(max(m, 0.0), 1.0/3.0);
    float s_ = pow(max(s, 0.0), 1.0/3.0);
    return vec3(
        0.2104542553*l_ + 0.7936177850*m_ - 0.0040720468*s_,
        1.9779984951*l_ - 2.4285922050*m_ + 0.4505937099*s_,
        0.0259040371*l_ + 0.7827717662*m_ - 0.8086757660*s_
    );
}

vec3 oklabToRgb(vec3 lab) {
    float l_ = lab.x + 0.3963377774*lab.y + 0.2158037573*lab.z;
    float m_ = lab.x - 0.1055613458*lab.y - 0.0638541728*lab.z;
    float s_ = lab.x - 0.0894841775*lab.y - 1.2914855480*lab.z;
    float l = l_*l_*l_;
    float m = m_*m_*m_;
    float s = s_*s_*s_;
    vec3 lin = vec3(
        +4.0767416621*l - 3.3077115913*m + 0.2309699292*s,
        -1.2684380046*l + 2.6097574011*m - 0.3413193965*s,
        -0.0041960863*l - 0.7034186147*m + 1.7076147010*s
    );
    return sqrt(max(lin, 0.0));
}

// ── HSL helpers ────────────────────────────────────────────────────

vec3 rgb2hsl(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    float d = mx - mn;
    float h = 0.0, s = 0.0, l = (mx + mn) * 0.5;
    if (d > 0.001) {
        s = (l <= 0.5) ? d / (mx + mn) : d / (2.0 - mx - mn);
        if      (c.r == mx) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (c.g == mx) h = (c.b - c.r) / d + 2.0;
        else                h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0; if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p)*6.0*t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p)*(2.0/3.0 - t)*6.0;
    return p;
}

vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s < 0.001) return vec3(l);
    float q = (l < 0.5) ? l*(1.0 + s) : l + s - l*s;
    float p = 2.0*l - q;
    return vec3(hue2rgb(p, q, h + 1.0/3.0), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0/3.0));
}

// ── Blend mode dispatch ────────────────────────────────────────────

vec4 applyBlend(int mode, vec4 dst, vec3 srcRGB, float srcA) {
    vec3 srcPremul = srcRGB * srcA;
    vec3 outRGB;
    float outA;
    if (dst.a <= 0.00000001) {
        return vec4(clamp(srcRGB, 0.0, 1.0), clamp(srcA, 0.0, 1.0));
    }

    if (mode == 0) {
        vec3 srcLin = srcRGB*srcRGB;
        vec3 dstLin = dst.rgb*dst.rgb;
        outRGB = sqrt(srcLin*srcA + dstLin*(1.0 - srcA));
        outA   = srcA + dst.a*(1.0 - srcA);
    } else if (mode == 1) {
        vec3 dstPremul = dst.rgb*dst.a;
        outA   = srcA + dst.a*(1.0 - srcA);
        outRGB = (outA > 0.00001)
            ? (srcPremul + dstPremul*(1.0 - srcA)) / outA
            : srcRGB;
    } else if (mode == 2) {
        vec3 srcLab = rgbToOklab(srcRGB);
        vec3 dstLab = rgbToOklab(dst.rgb);
        float wSrc  = srcA;
        float wDst  = dst.a*(1.0 - srcA);
        float wTotal = wSrc + wDst;
        vec3 blendedLab;
        if (wTotal > 0.00001) blendedLab = (srcLab*wSrc + dstLab*wDst) / wTotal;
        else blendedLab = srcLab;
        outRGB = oklabToRgb(blendedLab);
        outA   = wTotal;
    } else if (mode == 3) {
        outRGB = dst.rgb; outA = dst.a*(1.0 - srcA);
    } else if (mode == 4) {
        float eraseMask = dst.a*srcA;
        outRGB = mix(dst.rgb, srcRGB, eraseMask);
        outA   = dst.a*(1.0 - srcA*0.5);
    } else if (mode == 5) {
        outRGB = 1.0 - (1.0 - dst.rgb)*(1.0 - srcPremul);
        outA   = 1.0 - (1.0 - dst.a)*(1.0 - srcA);
    } else if (mode == 6) {
        outRGB = dst.rgb + srcPremul*(1.0 - dst.rgb);
        outA   = min(1.0, dst.a + srcA);
    } else if (mode == 7) {
        outRGB = max(dst.rgb, srcPremul);
        outA   = max(dst.a, srcA);
    } else if (mode == 8) {
        outRGB = min(dst.rgb, srcPremul);
        outA   = min(dst.a, srcA);
    } else if (mode == 9) {
        outRGB = 1.0 - (1.0 - dst.rgb) / (srcPremul + 0.001);
        outA   = min(1.0, dst.a + srcA);
    } else if (mode == 10) {
        outRGB = dst.rgb * mix(vec3(1.0), srcRGB, srcA);
        outA   = dst.a;
    } else if (mode == 11) {
        vec3 ov;
        if (dst.rgb.r < 0.5) ov.r = 2.0*dst.rgb.r*srcRGB.r;
        else ov.r = 1.0 - 2.0*(1.0 - dst.rgb.r)*(1.0 - srcRGB.r);
        if (dst.rgb.g < 0.5) ov.g = 2.0*dst.rgb.g*srcRGB.g;
        else ov.g = 1.0 - 2.0*(1.0 - dst.rgb.g)*(1.0 - srcRGB.g);
        if (dst.rgb.b < 0.5) ov.b = 2.0*dst.rgb.b*srcRGB.b;
        else ov.b = 1.0 - 2.0*(1.0 - dst.rgb.b)*(1.0 - srcRGB.b);
        outRGB = dst.rgb*(1.0 - srcA) + ov*srcA;
        outA   = srcA + dst.a*(1.0 - srcA);
    } else if (mode == 12) {
        vec3 sHSL = rgb2hsl(srcRGB);
        vec3 dHSL = rgb2hsl(dst.rgb);
        vec3 col  = hsl2rgb(vec3(sHSL.x, sHSL.y, dHSL.z));
        outRGB = dst.rgb*(1.0 - srcA) + col*srcA;
        outA   = srcA + dst.a*(1.0 - srcA);
    } else {
        outRGB = srcPremul + dst.rgb*(1.0 - srcA);
        outA   = srcA + dst.a*(1.0 - srcA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}

// ── Mode-aware bilinear fetch (gamma, oklab, or premultiplied) ──────
// px is in texelFetch pixel-space (Y=0 at bottom). Handles half-texel shift internally.
// seamless=true wraps coordinates with mod; false clamps to edge.

vec4 sampleBilinear(sampler2D tex, vec2 px, vec2 texSize, int bmidx, bool seamless) {
    vec2 fpx = px - vec2(0.5);
    vec2 ipx = floor(fpx);
    vec2 f   = fract(fpx);

    ivec2 c00, c10, c01, c11;
    if (seamless) {
        ivec2 sz = ivec2(texSize);
        c00 = ivec2(mod(ipx,                  vec2(sz)));
        c10 = ivec2(mod(ipx + vec2(1,0),      vec2(sz)));
        c01 = ivec2(mod(ipx + vec2(0,1),      vec2(sz)));
        c11 = ivec2(mod(ipx + vec2(1,1),      vec2(sz)));
    } else {
        ivec2 maxC = ivec2(texSize - vec2(1.0));
        c00 = clamp(ivec2(ipx),              ivec2(0), maxC);
        c10 = clamp(ivec2(ipx) + ivec2(1,0), ivec2(0), maxC);
        c01 = clamp(ivec2(ipx) + ivec2(0,1), ivec2(0), maxC);
        c11 = clamp(ivec2(ipx) + ivec2(1,1), ivec2(0), maxC);
    }

    vec4 tl = texelFetch(tex, c00, 0);
    vec4 tr = texelFetch(tex, c10, 0);
    vec4 bl = texelFetch(tex, c01, 0);
    vec4 br = texelFetch(tex, c11, 0);

    if (bmidx == 0) {
        vec3 tl_g = tl.rgb*tl.rgb;
        vec3 tr_g = tr.rgb*tr.rgb;
        vec3 bl_g = bl.rgb*bl.rgb;
        vec3 br_g = br.rgb*br.rgb;
        vec3 mixed = mix(mix(tl_g, tr_g, f.x), mix(bl_g, br_g, f.x), f.y);
        float a    = mix(mix(tl.a, tr.a, f.x), mix(bl.a, br.a, f.x), f.y);
        return vec4(sqrt(max(mixed, vec3(0.0))), a);
    } else if (bmidx == 2) {
        vec3 ll = rgbToOklab(tl.rgb);
        vec3 lr = rgbToOklab(tr.rgb);
        vec3 bl2 = rgbToOklab(bl.rgb);
        vec3 br2 = rgbToOklab(br.rgb);
        vec3 mixed = mix(mix(ll, lr, f.x), mix(bl2, br2, f.x), f.y);
        float a    = mix(mix(tl.a, tr.a, f.x), mix(bl.a, br.a, f.x), f.y);
        return vec4(oklabToRgb(mixed), a);
    } else {
        vec4 tl_p = vec4(tl.rgb*tl.a, tl.a);
        vec4 tr_p = vec4(tr.rgb*tr.a, tr.a);
        vec4 bl_p = vec4(bl.rgb*bl.a, bl.a);
        vec4 br_p = vec4(br.rgb*br.a, br.a);
        vec4 mixed = mix(mix(tl_p, tr_p, f.x), mix(bl_p, br_p, f.x), f.y);
        vec3 rgb = mixed.a > 0.00001 ? mixed.rgb / mixed.a : mixed.rgb;
        return vec4(rgb, mixed.a);
    }
}
