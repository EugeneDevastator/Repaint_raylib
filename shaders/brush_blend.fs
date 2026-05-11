#version 330

// ============================================================================
// FULL BRUSH MASK SHADER
// - Hardness (rad_in / rad_out ratio)
// - Curvature (bubble/pinch)
// - Aspect ratio (x2y)
// - Rotation (resangle)
// - Solidity noise scatter
// - Multiple blend modes
// ============================================================================

in vec2 fragTexCoord;
in vec4 fragColor;
out vec4 finalColor;

uniform sampler2D texture0;

uniform vec4 rectBounds;
uniform float radIn;
uniform float radOut;
uniform float opacity;
uniform vec4 brushColor;
uniform float crv;
uniform float x2y;
uniform float resangle;
uniform float sol;
uniform float sol2op;
uniform int bmidx;
uniform float seed;

float crPinchFunc(float val) {
    return val * val * val;
}

float crBubFunc(float val) {
    return 1.0 + (val - 1.0) * (val - 1.0) * (val - 1.0);
}

float crContFunc(float val, float mid) {
    if (val <= mid) {
        float t = (mid > 0.001) ? val / mid : 0.0;
        return t * t * t * mid;
    } else {
        float range = 1.0 - mid;
        float t = (range > 0.001) ? (val - mid) / range : 0.0;
        return mid + (1.0 + (t - 1.0) * (t - 1.0) * (t - 1.0)) * range;
    }
}

float hash2(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = fragTexCoord;
    vec4 dst = texture(texture0, uv);

    vec2 rMin = rectBounds.xy;
    vec2 rMax = rectBounds.xy + rectBounds.zw;

    if (uv.x < rMin.x || uv.x > rMax.x || uv.y < rMin.y || uv.y > rMax.y) {
        finalColor = dst;
        return;
    }

    vec2 center = (rMin + rMax) * 0.5;
    vec2 halfSize = (rMax - rMin) * 0.5;

    vec2 diff;
    diff.x = (uv.x - center.x) / max(halfSize.x, 0.001);
    diff.y = (uv.y - center.y) / max(halfSize.y, 0.001);

    // Aspect ratio (x2y): scale X to stretch/compress
    float aspect = max(x2y, 0.01);
    diff.x /= aspect;

    // Rotation (resangle)
    float angle = radians(resangle);
    float ca = cos(angle);
    float sa = sin(angle);
    vec2 rdiff;
    rdiff.x = diff.x * ca - diff.y * sa;
    rdiff.y = diff.x * sa + diff.y * ca;

    float dist = length(rdiff);

    if (dist > 1.0) {
        finalColor = dst;
        return;
    }

    // Hardness: t = normalized distance, innerT = fraction fully solid
    float t = dist;
    float innerT = clamp(radIn / max(radOut, 0.001), 0.0, 1.0);

    float alpha = 1.0;
    if (t > innerT && radOut > 0.001) {
        float edgeRange = 1.0 - innerT;
        if (edgeRange > 0.001) {
            alpha = 1.0 - (t - innerT) / edgeRange;
        }
    }
    alpha = clamp(alpha, 0.0, 1.0);
    float rawAlpha = alpha;

    // Contrast remap
    float top = 1.0 - innerT;
    if (top < 0.001) top = 0.001;
    float mid = innerT;
    //alpha = crContFunc(clamp(alpha / top, 0.0, 1.0), mid);
    alpha = clamp(alpha, 0.0, 1.0);
	float crvt = crv*2.0-1.0;
	float curvePower = (crvt >= 0.0)
		? mix(1.0, 3.0, crvt)
		: mix(1.0, 1.0/3.0, -crvt);
	alpha = pow(alpha, curvePower);
	alpha = clamp(alpha, 0.0, 1.0);


    // Solidity noise scatter
    if (!(sol >= 0.999 && sol2op <= 0.001)) {
        float noiseVal = hash2(uv * 1000.0 + floor(seed * 1000.0));
        float fsol2op = clamp(1.0 - sol2op, 0.0, 1.0);
        float nsal = (noiseVal < sol) ? 1.0 : 0.0;
        float noiseres = (noiseVal < alpha) ? 1.0 : 0.0;
        nsal = noiseres * fsol2op + nsal * (1.0 - fsol2op);
        alpha *= nsal;
    }

    alpha = clamp(alpha, 0.0, 1.0);
    float finalAlpha = alpha * opacity;

    if (finalAlpha < 0.001) {
        finalColor = dst;
        return;
    }

    // Premultiplied src
    vec3 srcRGB = brushColor.rgb;
    float srcA = finalAlpha;
    vec3 srcPremul = srcRGB * srcA;

    // Blend modes
    vec3 outRGB;
    float outA;

    if (bmidx == 0) {    // normal -gamma encoded
		vec3 srcLin = srcRGB * srcRGB; // gamma decode (approx sRGB -> linear)
		vec3 dstLin = dst.rgb * dst.rgb;
		vec3 blended = srcLin * srcA + dstLin * (1.0 - srcA);
		outRGB = sqrt(blended);               // gamma re-encode
		outA   = srcA + dst.a * (1.0 - srcA);

    } else if (bmidx == 1) { 
	    // bmPlus
        outRGB = dst.rgb + srcPremul;
        outA = min(1.0, dst.a + srcA);
    } else if (bmidx == 2) { // bmDodge
        outRGB = dst.rgb + srcPremul * (1.0 - dst.rgb);
        outA = min(1.0, dst.a + srcA);
    } else if (bmidx == 3) { // bmScreen
        outRGB = 1.0 - (1.0 - dst.rgb) * (1.0 - srcPremul);
        outA = 1.0 - (1.0 - dst.a) * (1.0 - srcA);
    } else if (bmidx == 4) { // bmLighten
        outRGB = max(dst.rgb, srcPremul);
        outA = max(dst.a, srcA);
    } else if (bmidx == 5) { // bmBurn
        outRGB = 1.0 - (1.0 - dst.rgb) / (srcPremul + 0.001);
        outA = min(1.0, dst.a + srcA);
    } else if (bmidx == 6) { // bmMult
        outRGB = dst.rgb * mix(vec3(1.0), srcRGB, srcA);
        outA = dst.a * mix(1.0, srcA, srcA);
    } else if (bmidx == 7) { // bmDarken
        outRGB = min(dst.rgb, srcPremul);
        outA = min(dst.a, srcA);
    } else if (bmidx == 8) { 
	  // bmNormal
        outRGB = srcPremul + dst.rgb * (1.0 - srcA);
        outA = srcA + dst.a * (1.0 - srcA);
    } else { // default: normal
        outRGB = srcPremul + dst.rgb * (1.0 - srcA);
        outA = srcA + dst.a * (1.0 - srcA);
    }

    finalColor = vec4(outRGB, outA);
}
