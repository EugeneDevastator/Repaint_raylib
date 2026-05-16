#version 330

in vec2 fragTexCoord;
in vec4 fragColor;
in vec2 localUV;
out vec4 finalColor;

uniform sampler2D texture0;

uniform float radIn;
uniform float radOut;
uniform float opacity;
uniform float x2y;
uniform float resangle;
uniform float useTex;
uniform sampler2D userMaskTex;
uniform float maskMode;
uniform float maskMix;
uniform float texScale;
uniform float texFeather;

void main() {
    vec4 canvas = texture(texture0, fragTexCoord);

    vec2 p = localUV * 2.0 - 1.0;

    float angle = radians(resangle);
    float c = cos(angle), s = sin(angle);
    vec2 bp;
    bp.x = p.x * c - p.y * s;
    bp.y = p.x * s + p.y * c;
    bp.x /= max(x2y, 0.01);

    float dist = length(bp);
    float inner = clamp(radIn / max(radOut, 0.001), 0.0, 1.0);

    vec2 texUV = bp * 0.5 + 0.5;
    float brushAlpha;
    vec3 brushColor;

    if (useTex > 0.5) {
        vec4 texColor = texture(userMaskTex, texUV * texScale);

        float maskAlpha;
        if (maskMode > 0.5)
            maskAlpha = dot(texColor.rgb, vec3(0.299, 0.587, 0.114));
        else
            maskAlpha = texColor.a;
        maskAlpha = clamp(maskAlpha, 0.0, 1.0);
        brushColor = texColor.rgb;

        if (maskMix > 1.5) {
            // use tex mask: mask feeds into inner radius + curve
            // clip to stamp rect via localUV bounds instead of circle SDF
            if (any(lessThan(localUV, vec2(0.0))) || any(greaterThan(localUV, vec2(1.0)))) {
                finalColor = canvas;
                return;
            }
            brushAlpha = 1.0 - smoothstep(inner, 1.0, maskAlpha);
        } else {
            if (dist > 1.0) { finalColor = canvas; return; }
            brushAlpha = 1.0 - smoothstep(inner, 1.0, dist);
            brushAlpha = clamp(brushAlpha, 0.0, 1.0);
            if (maskMix > 0.5) {
                float lo = maskAlpha - texFeather;
                float hi = maskAlpha + texFeather;
                brushAlpha = smoothstep(lo, hi, brushAlpha);
            } else {
                brushAlpha = brushAlpha * maskAlpha;
            }
        }
    } else {
        if (dist > 1.0) { finalColor = canvas; return; }
        brushAlpha = 1.0 - smoothstep(inner, 1.0, dist);
        brushAlpha = clamp(brushAlpha, 0.0, 1.0);
        brushColor = vec3(texUV, 0.0);
    }

    float finalAlpha = clamp(brushAlpha, 0.0, 1.0) * opacity;
    if (finalAlpha < 0.0001) { finalColor = canvas; return; }

    finalColor.rgb = brushColor.rgb * finalAlpha + canvas.rgb * (1.0 - finalAlpha);
    finalColor.a = finalAlpha + canvas.a * (1.0 - finalAlpha);
}
