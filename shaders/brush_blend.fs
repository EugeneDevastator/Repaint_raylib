#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D geoTex;
uniform sampler2D canvasTex;
uniform sampler2D brushTex;
uniform sampler2D smudgeTex;
uniform bool     hasSmudge;

uniform vec2  copyOrigin;
uniform vec2  copySize;
uniform float opacity;
uniform float sol;
uniform float sol2op;
uniform float seed;
uniform float preserveop;
uniform float smudgeStrength;
uniform vec2  smudgeOffsetUV;
uniform float texBlendVal;
uniform float texScale;
uniform vec2  texOffset;
uniform vec2  userTexOrigin;
uniform float texFeather;
uniform float texThresh;
uniform int   texNoisemode;
uniform bool  useLumAsAlpha;
uniform bool  hasTexture;
uniform int   texColorMode;
uniform int   bmidx;
uniform vec4  brushColor;
uniform float radOut;
uniform vec2  stampCenter;
uniform vec2  canvasSize;
uniform float pwr;
uniform int   eraseMode;
uniform bool  uSeamless;
in vec2 canvasFragUV;
in vec2 outCanvasPx;

float srgbToLinear(float c) {
    return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}
float linearToSrgb(float c) {
    return (c <= 0.0031308) ? c * 12.92 : 1.055 * pow(c, 1.0/2.4) - 0.055;
}
vec3 srgbToLinear3(vec3 c) { return vec3(srgbToLinear(c.r), srgbToLinear(c.g), srgbToLinear(c.b)); }
vec3 linearToSrgb3(vec3 c) { return vec3(linearToSrgb(c.r), linearToSrgb(c.g), linearToSrgb(c.b)); }
						   // sRGB -> OKLab
vec3 rgbToOklab(vec3 c) {
    vec3 lin = clamp(c, 0.0, 1.0);
    // linear RGB -> LMS (OKLab's cone space)
    float l = 0.4122214708 * lin.r + 0.5363325363 * lin.g + 0.0514459929 * lin.b;
    float m = 0.2119034982 * lin.r + 0.6806995451 * lin.g + 0.1073969566 * lin.b;
    float s = 0.0883024619 * lin.r + 0.2817188376 * lin.g + 0.6299787005 * lin.b;

    // cube root (perceptual compression)
    float l_ = pow(max(l, 0.0), 1.0/3.0);
    float m_ = pow(max(m, 0.0), 1.0/3.0);
    float s_ = pow(max(s, 0.0), 1.0/3.0);

    // LMS -> Lab
    return vec3(
        0.2104542553 * l_ + 0.7936177850 * m_ - 0.0040720468 * s_,
        1.9779984951 * l_ - 2.4285922050 * m_ + 0.4505937099 * s_,
        0.0259040371 * l_ + 0.7827717662 * m_ - 0.8086757660 * s_
    );
}

// OKLab -> sRGB
vec3 oklabToRgb(vec3 lab) {
    // Lab -> LMS
    float l_ = lab.x + 0.3963377774 * lab.y + 0.2158037573 * lab.z;
    float m_ = lab.x - 0.1055613458 * lab.y - 0.0638541728 * lab.z;
    float s_ = lab.x - 0.0894841775 * lab.y - 1.2914855480 * lab.z;

    // undo cube root
    float l = l_ * l_ * l_;
    float m = m_ * m_ * m_;
    float s = s_ * s_ * s_;

    // LMS -> linear RGB
    vec3 lin = vec3(
        +4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    );

    // linear -> sRGB (fast approx)
    return clamp(lin, 0.0, 1.0);  // already linear, no linearToSrgb
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
// Returns hard or soft threshold cut based on texFeather
// combined: input mask value [0..1]
// cut: abs(texThresh), threshold level
// texFeather: softness of edge (0 = hard cut)
float applyThreshold(float combined, float cut, float texFeather) {
    float edgeDist = combined - cut;
    if (texFeather <= 0.0)
        return (edgeDist > 0.0) ? 1.0 : 0.0;
    return clamp(edgeDist / max(texFeather, 0.0001), 0.0, 1.0);
}

// ── HSL helpers for Color mode ──────────────────────────────────────
vec3 rgb2hsl(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b));
    float d = mx - mn;
    float h = 0.0, s = 0.0, l = (mx + mn) * 0.5;
    if (d > 0.001) {
        s = (l <= 0.5) ? d / (mx + mn) : d / (2.0 - mx - mn);
        if (c.r == mx)      h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
        else if (c.g == mx) h = (c.b - c.r) / d + 2.0;
        else                h = (c.r - c.g) / d + 4.0;
        h /= 6.0;
    }
    return vec3(h, s, l);
}

float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0/6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0/2.0) return q;
    if (t < 2.0/3.0) return p + (q - p) * (2.0/3.0 - t) * 6.0;
    return p;
}

vec3 hsl2rgb(vec3 hsl) {
    float h = hsl.x, s = hsl.y, l = hsl.z;
    if (s < 0.001) return vec3(l);
    float q = (l < 0.5) ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p, q, h + 1.0/3.0), hue2rgb(p, q, h), hue2rgb(p, q, h - 1.0/3.0));
}

vec4 applyBlend(int mode, vec4 canvas, vec3 brushRGB, float brushA) {
    vec3 brushPremul = brushRGB * brushA;
    vec3 outRGB;
    float outA;

    if (canvas.a <= 0.00000001) {
        if (mode == 2 || mode == 3) // EraseA / EraseColor — nothing to erase
            return vec4(canvas.rgb, 0.0);
        return vec4(clamp(brushRGB, 0.0, 1.0), clamp(brushA, 0.0, 1.0));
    }
    // older N-gamma, dont delete.
    //  if (mode == 0) { // N-Gamma
    //    vec3 brushLin  = brushRGB * brushRGB;
    //    vec3 canvasLin = canvas.rgb * canvas.rgb;
    //    outRGB = sqrt(brushLin * brushA + canvasLin * (1.0 - brushA));
    //    outA   = brushA + canvas.a * (1.0 - brushA);
    //      }
if (mode == 0) { // N-OKLab
    vec3 brushLab  = rgbToOklab(brushRGB);
    vec3 canvasLab = rgbToOklab(canvas.rgb);

    // Correct Porter-Duff weights
    float wBrush  = brushA;
    float wCanvas = canvas.a * (1.0 - brushA);
    float wTotal  = wBrush + wCanvas; // same as outA

    vec3 blendedLab;
    if (wTotal > 0.00001) {
        // Weighted blend in Lab space, normalized by total coverage
        blendedLab = (brushLab * wBrush + canvasLab * wCanvas) / wTotal;
    } else {
        blendedLab = brushLab;
    }

    outRGB = oklabToRgb(blendedLab);
    outA   = wTotal; // brushA + canvas.a * (1.0 - brushA)
} else if (mode == 1) { // N-Linear
		vec3 brushPremul  = brushRGB * brushA;
		vec3 canvasPremul = canvas.rgb * canvas.a;

		outA   = brushA + canvas.a * (1.0 - brushA);
		outRGB = (outA > 0.00001) 
			? (brushPremul + canvasPremul * (1.0 - brushA)) / outA 
			: brushRGB;
    }

        else if (mode == 4) { // N-Gamma
            vec3 brushLin  = brushRGB * brushRGB;
            vec3 canvasLin = canvas.rgb * canvas.rgb;
            outRGB = sqrt(brushLin * brushA + canvasLin * (1.0 - brushA));
            outA   = brushA + canvas.a * (1.0 - brushA);
              }
    else if (mode == 2) { // EraseA
        outRGB = canvas.rgb;
        outA   = canvas.a * (1.0 - brushA);
    } else if (mode == 3) { // EraseColor
        float eraseMask = canvas.a * brushA;
        outRGB = mix(canvas.rgb, brushRGB, eraseMask);
        outA   = canvas.a * (1.0 - brushA * 0.5);
    } else if (mode == 444) { // Screen
        outRGB = 1.0 - (1.0 - canvas.rgb) * (1.0 - brushPremul);
        outA   = 1.0 - (1.0 - canvas.a) * (1.0 - brushA);
    } else if (mode == 5) { // Color Dodge
        outRGB = canvas.rgb + brushPremul * (1.0 - canvas.rgb);
        outA   = min(1.0, canvas.a + brushA);
    } else if (mode == 6) { // Lighten
        outRGB = max(canvas.rgb, brushPremul);
        outA   = max(canvas.a, brushA);
    } else if (mode == 7) { // Darken
        outRGB = min(canvas.rgb, brushPremul);
        outA   = min(canvas.a, brushA);
    } else if (mode == 8) { // Burn
        outRGB = 1.0 - (1.0 - canvas.rgb) / (brushPremul + 0.001);
        outA   = min(1.0, canvas.a + brushA);
    } else if (mode == 9) { // Multiply
        outRGB = canvas.rgb * mix(vec3(1.0), brushRGB, brushA);
        outA   = canvas.a;
    } else if (mode == 10) { // Overlay
        vec3 ov;
        if (canvas.rgb.r < 0.5) ov.r = 2.0 * canvas.rgb.r * brushRGB.r;
        else ov.r = 1.0 - 2.0 * (1.0 - canvas.rgb.r) * (1.0 - brushRGB.r);
        if (canvas.rgb.g < 0.5) ov.g = 2.0 * canvas.rgb.g * brushRGB.g;
        else ov.g = 1.0 - 2.0 * (1.0 - canvas.rgb.g) * (1.0 - brushRGB.g);
        if (canvas.rgb.b < 0.5) ov.b = 2.0 * canvas.rgb.b * brushRGB.b;
        else ov.b = 1.0 - 2.0 * (1.0 - canvas.rgb.b) * (1.0 - brushRGB.b);
        outRGB = canvas.rgb * (1.0 - brushA) + ov * brushA;
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else if (mode == 11) { // Color
        vec3 bHSL = rgb2hsl(brushRGB);
        vec3 cHSL = rgb2hsl(canvas.rgb);
        vec3 col = hsl2rgb(vec3(bHSL.x, bHSL.y, cHSL.z));
        outRGB = canvas.rgb * (1.0 - brushA) + col * brushA;
        outA   = brushA + canvas.a * (1.0 - brushA);
    } else {
        outRGB = brushPremul + canvas.rgb * (1.0 - brushA);
        outA   = brushA + canvas.a * (1.0 - brushA);
    }

    return vec4(clamp(outRGB, 0.0, 1.0), clamp(outA, 0.0, 1.0));
}
// oklab compatible blender
vec4 sampleCopy(sampler2D tex, vec2 canvasPx) {
    if (true) // force nearest for now
    {
        vec2 px = canvasPx - copyOrigin;
        vec2 ipx = floor(px);
        vec2 f = fract(px);
        // Bounds check BEFORE bilinear — return transparent if outside
        if (px.x < -0.5 || px.y < -0.5 ||
            px.x >= copySize.x || px.y >= copySize.y)
            return vec4(0.0); // or canvas color — signals "no data"
        ivec2 maxCoord = ivec2(copySize - vec2(1.0));
        ivec2 c = clamp(ivec2(ipx), ivec2(0), maxCoord);
        return texelFetch(tex, c, 0);
    }
    // vec2 px = canvasPx - copyOrigin - 0.5;
    vec2 px = canvasPx - copyOrigin - 0.5;
    vec2 ipx = floor(px);
    vec2 f = fract(px);
					      // Bounds check BEFORE bilinear — return transparent if outside
    if (px.x < -0.5 || px.y < -0.5 ||
        px.x >= copySize.x || px.y >= copySize.y)
        return vec4(0.0); // or canvas color — signals "no data"

    ivec2 maxCoord = ivec2(copySize - vec2(1.0));

    ivec2 c00 = clamp(ivec2(ipx),              ivec2(0), maxCoord);
    ivec2 c10 = clamp(ivec2(ipx) + ivec2(1,0), ivec2(0), maxCoord);
    ivec2 c01 = clamp(ivec2(ipx) + ivec2(0,1), ivec2(0), maxCoord);
    ivec2 c11 = clamp(ivec2(ipx) + ivec2(1,1), ivec2(0), maxCoord);

    vec4 tl = texelFetch(tex, c00, 0);
    vec4 tr = texelFetch(tex, c10, 0);
    vec4 bl = texelFetch(tex, c01, 0);
    vec4 br = texelFetch(tex, c11, 0);

    if (bmidx == 0) {
        // convert all 4 to Lab, mix, convert back
        vec3 ll = rgbToOklab(tl.rgb);
        vec3 lr = rgbToOklab(tr.rgb);
        vec3 bl2 = rgbToOklab(bl.rgb);
        vec3 br2 = rgbToOklab(br.rgb);

        vec3 mixed = mix(mix(ll, lr, f.x), mix(bl2, br2, f.x), f.y);
        float a = mix(mix(tl.a, tr.a, f.x), mix(bl.a, br.a, f.x), f.y);
        return vec4(oklabToRgb(mixed), a);
    } else if (bmidx == 4) {
        // sqrt-gamma bilinear
        vec3 tl_lin = tl.rgb * tl.rgb;
        vec3 tr_lin = tr.rgb * tr.rgb;
        vec3 bl_lin = bl.rgb * bl.rgb;
        vec3 br_lin = br.rgb * br.rgb;
        vec3 mixed = mix(mix(tl_lin, tr_lin, f.x), mix(bl_lin, br_lin, f.x), f.y);
        float a = mix(mix(tl.a, tr.a, f.x), mix(bl.a, br.a, f.x), f.y);
        return vec4(sqrt(mixed), a);
} else {
    // Premultiplied bilinear to avoid dark-fringe bleed
    vec4 tl_p = vec4(tl.rgb * tl.a, tl.a);
    vec4 tr_p = vec4(tr.rgb * tr.a, tr.a);
    vec4 bl_p = vec4(bl.rgb * bl.a, bl.a);
    vec4 br_p = vec4(br.rgb * br.a, br.a);
    vec4 mixed = mix(mix(tl_p, tr_p, f.x), mix(bl_p, br_p, f.x), f.y);
    // Un-premultiply
    return vec4(mixed.a > 0.00001 ? mixed.rgb / mixed.a : mixed.rgb, mixed.a);
}

}
// Convert canvas-normalised UV (canvasFragUV) to the stamp-region copy UV
vec2 toCopyUV(vec2 cuv) {
    vec2 px;
    px.x = cuv.x * canvasSize.x;
    px.y = (1.0 - cuv.y) * canvasSize.y;
    return (px - copyOrigin) / copySize;
}

void main() {
    vec2 uv       = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);
    vec4 geouv    = texture(geoTex, sampleUV);
        if (geouv.a < 0.000001) {
    		discard;
        }

    vec4 canvas = uSeamless
        ? sampleCopy(canvasTex, mod(outCanvasPx, canvasSize))
        : sampleCopy(canvasTex, outCanvasPx);

   //if (!uSeamless) {
   //    vec2 localPx = outCanvasPx - copyOrigin - 0.5;
   //    if (localPx.x < 0.0 || localPx.y < 0.0 ||
   //        localPx.x >= copySize.x || localPx.y >= copySize.y) {
   //        discard;
   //    }
   //}
    float alpha = geouv.a;

    // --- texture sampling ---
    float finalAlpha;
    vec3 brushFinal;

    if (hasTexture) {
        vec2 stUV = geouv.rg;

        if (texNoisemode == 0) {
            stUV = (canvasFragUV - 0.5) * canvasSize / 256.0 * texScale + userTexOrigin;
        } else if (texNoisemode == 1) {
            stUV = geouv.rg * texScale + texOffset;
        } else {
            stUV = (geouv.rg - 0.5) * texScale + userTexOrigin;
        }
        vec4 texel = texture(brushTex, stUV);

        float userTexA = useLumAsAlpha
            ? (texel.r + texel.g + texel.b) * (1.0 / 3.0)
            : texel.a;

        if (texThresh < 0.0)
            userTexA = 1.0 - userTexA;

        finalAlpha = clamp(applyThreshold(alpha * userTexA, abs(texThresh), texFeather), 0.0, 1.0);

        if (texColorMode == 0) {
            brushFinal = brushColor.rgb;
        } else if (texColorMode == 1) {
            brushFinal = texel.rgb;
        } else if (texColorMode == 2) {
            brushFinal = texel.rgb * brushColor.rgb;
    } else {
        // lum-color (3): black → brushColor → white via luminance
        float lum = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
        if (lum < 0.5)
            brushFinal = mix(vec3(0.0), brushColor.rgb, lum * 2.0);
        else
            brushFinal = mix(brushColor.rgb, vec3(1.0), (lum - 0.5) * 2.0);
        }
    } else {
        finalAlpha = alpha;
        brushFinal = brushColor.rgb;
    }

    finalAlpha *= opacity;
    if (finalAlpha < 0.00001) { finalColor = canvas; return; }

    // ── Erase modes ─────────────────────────────────────────────────
    if (eraseMode == 1) {
        // Alpha erase: reduce canvas alpha by brush alpha
        float newA = canvas.a * (1.0 - finalAlpha);
        finalColor = vec4(canvas.rgb, newA);
        return;
    } else if (eraseMode == 2) {
        // Color erase: apply color proportional to alpha, then erase
        float mask = canvas.a * finalAlpha;
        vec3 erasedRGB = mix(canvas.rgb, brushFinal, mask);
        float erasedA = canvas.a * (1.0 - finalAlpha * 0.5);
        finalColor = vec4(erasedRGB, erasedA);
        return;
    }

float cloneOpacity = smudgeStrength;
    if (cloneOpacity > 0.000001) {

    vec2 srcPx;
    if (hasSmudge) {
        // smudgeTex is pre-shifted — use same position as canvas read
        srcPx = uSeamless ? mod(outCanvasPx, canvasSize) : outCanvasPx;
    } else {
        srcPx = outCanvasPx - smudgeOffsetUV;
        if (uSeamless)
            srcPx = mod(srcPx, canvasSize);
        else
            srcPx = clamp(srcPx, copyOrigin, copyOrigin + copySize - vec2(1.0));
    }

        vec4 smudgeSample = hasSmudge
            ? sampleCopy(smudgeTex, srcPx)
            : sampleCopy(canvasTex, srcPx);
        float smudgeA = smudgeSample.a;

// gracefully handle transparent layers to not mix with blacks.
        vec3 smudgeRGB;
        if (smudgeA < 0.0000001)
            smudgeRGB = canvas.rgb;
         else
            smudgeRGB = smudgeSample.rgb;

        //vec4 smudgeCol = vec4(smudgeRGB, 1);

        // just do color application after. as separate pass.
        // if (smudgeStrength >= 0.9999999) {
            brushFinal = smudgeRGB;
        // } else {
        //            float w = (1.0 - smudgeStrength) * (1.0 - smudgeStrength);
        //          brushFinal = applyBlend(bmidx, smudgeCol, brushFinal, w).rgb;
        //    }

        // this section works better for linear gamma
        // float w = cloneOpacity * finalAlpha; // attenuate
        // finalColor = applyBlend(bmidx, canvas, brushFinal, w);


       /// finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
// Forced color calcs for smudge.
    vec3 mixedRGB;
    if (bmidx == 0) {
        vec3 labCanvas = rgbToOklab(canvas.rgb);
        vec3 labSmudge = rgbToOklab(smudgeRGB);
        mixedRGB = oklabToRgb(mix(labCanvas, labSmudge, finalAlpha));
    } else if (bmidx == 4) {
        vec3 linCanvas = canvas.rgb * canvas.rgb;
        vec3 linSmudge = smudgeRGB * smudgeRGB;
        mixedRGB = sqrt(mix(linCanvas, linSmudge, finalAlpha));
    } else {
        // linear or anything else — straight lerp, no premul
        mixedRGB = mix(canvas.rgb, smudgeRGB, finalAlpha);
    }

    finalColor = vec4(mixedRGB, canvas.a);

        // Alpha: treat canvas A as plain channel, lerp smudge alpha into canvas alpha weighted by brush mask
        float blendedA = mix(canvas.a, smudgeA, finalAlpha);

           if (preserveop > 0.5)
            finalColor.a = canvas.a;
           else
            finalColor.a = blendedA;

			finalColor.a = canvas.a;
        return;
    }

    finalColor = applyBlend(bmidx, canvas, brushFinal, finalAlpha);
        if (preserveop > 0.5) finalColor.a = canvas.a;
}
