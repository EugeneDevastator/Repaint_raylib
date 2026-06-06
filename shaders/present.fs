#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D presentTex;
uniform vec2      texSize;
uniform bool      applyDither;

#include blend.glsl

// ── Spatial hash for dither ──────────────────────────────────────────
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    vec4 col = sampleBilinear(presentTex, fragTexCoord * texSize, texSize, 0, false);

    // Spatial dither — breaks up banding (screen only)
    float off = 0.0;
    if (applyDither) {
        float d = hash21(floor(gl_FragCoord.xy));
        off = (d - 0.5) / 255.0;
    }

    finalColor = vec4(clamp(col.rgb + off, 0.0, 1.0), col.a);
}
