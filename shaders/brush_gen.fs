#version 330

// Brush generation shader - replaces scanline methods from ArtMaster.cpp
in vec2 fragTexCoord;
out vec4 finalColor;

uniform vec2 center;
uniform float rad_in;
uniform float rad_out;
uniform float crv; // curvature
uniform vec4 brushColor;
uniform int pipeID;
uniform float time;

// Noise texture for solidity/noise effects
uniform sampler2D noiseTex;

void main() {
    vec2 uv = fragTexCoord;
    vec2 diff = uv - center;
    float dist = length(diff);

    // Create radial gradient brush shape
    float t = clamp(dist / rad_out, 0.0, 1.0);

    // Inner radius cutoff
    float innerT = rad_in / rad_out;
    float shape = 1.0 - smoothstep(innerT - 0.01, innerT + 0.01, t);

    // Apply curvature (crv) - similar to GenCurveF
    float curveFactor = t;
    if (crv < 0.0) {
        float mid = 1.0 - crv;
        float fpos = 1.0 + pow(t - 1.0, 3.0);
        curveFactor = mix(t, fpos, abs(crv));
    } else if (crv > 0.0) {
        curveFactor = mix(t, t * t * t, crv);
    }

    // Final alpha based on brush shape and curve
    float alpha = shape * (1.0 - curveFactor);

    // Apply noise/solidity if pipeID indicates it
    if (pipeID == 0) { // plCFNSR
        vec2 noiseCoord = uv * 2.0 + vec2(time * 0.1);
        float noise = texture(noiseTex, noiseCoord).r;
        // Simple solidity - threshold based on noise
        float solidity = 0.5; // configurable
        alpha *= step(1.0 - solidity, noise);
    }

    finalColor = vec4(brushColor.rgb, brushColor.a * alpha);
}
