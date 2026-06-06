#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D presentTex;
uniform vec2      texSize;
uniform bool      applyDither;

// ── Spatial hash for dither ──────────────────────────────────────────
float hash21(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

void main() {
    // Perceptual bilinear: sample 4 texels, interpolate in gamma space
    vec2 px = fragTexCoord * texSize - vec2(0.5);
    vec2 ipx = floor(px);
    vec2 f   = fract(px);
    ivec2 maxC = ivec2(texSize - vec2(1.0));
    ivec2 c00 = clamp(ivec2(ipx),              ivec2(0), maxC);
    ivec2 c10 = clamp(ivec2(ipx) + ivec2(1,0), ivec2(0), maxC);
    ivec2 c01 = clamp(ivec2(ipx) + ivec2(0,1), ivec2(0), maxC);
    ivec2 c11 = clamp(ivec2(ipx) + ivec2(1,1), ivec2(0), maxC);

    vec4 tl = texelFetch(presentTex, c00, 0);
    vec4 tr = texelFetch(presentTex, c10, 0);
    vec4 bl = texelFetch(presentTex, c01, 0);
    vec4 br = texelFetch(presentTex, c11, 0);

    // N-Gamma: square → mix → sqrt (perceptual interpolation)
    vec3 tl_g = tl.rgb * tl.rgb;
    vec3 tr_g = tr.rgb * tr.rgb;
    vec3 bl_g = bl.rgb * bl.rgb;
    vec3 br_g = br.rgb * br.rgb;
    vec3 mixed = mix(mix(tl_g, tr_g, f.x), mix(bl_g, br_g, f.x), f.y);
    float a    = mix(mix(tl.a, tr.a, f.x), mix(bl.a, br.a, f.x), f.y);

    vec4 col;
    col.rgb = sqrt(max(mixed, vec3(0.0)));
    col.a   = a;

    // Spatial dither — breaks up banding (screen only)
    float off = 0.0;
    if (applyDither) {
        float d = hash21(floor(gl_FragCoord.xy));
        off = (d - 0.5) / 255.0;
    }

    finalColor = vec4(clamp(col.rgb + off, 0.0, 1.0), col.a);
}
