#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform vec2 texSize;
uniform vec2 center;
uniform float rad_in;
uniform float rad_out;
uniform float crv;
uniform int pipeID;
uniform float sol;
uniform float sol2op;
uniform float sigma;
uniform float time;

float crPinchFunc(float val) { return val * val * val; }

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

float hash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    vec2 uv = fragTexCoord * texSize;
    vec2 diff = uv - center;
    float dist = length(diff);

    if (dist >= rad_out) {
        finalColor = vec4(1.0, 1.0, 1.0, 0.0);
        return;
    }

    float t = dist / rad_out;
    float innerT = rad_in / rad_out;

    float alpha = 1.0;
    if (t > innerT && rad_out > rad_in) {
        alpha = 1.0 - (t - innerT) / (1.0 - innerT);
    }
    alpha = clamp(alpha, 0.0, 1.0);

    if (crv < 0.0) {
        float cube = (t - 1.0) * (t - 1.0) * (t - 1.0);
        float fpos = 1.0 + cube;
        alpha = (fpos - t) * (-crv) + t;
        alpha = clamp(alpha, 0.0, 1.0);
    } else if (crv > 0.0) {
        alpha = (t * t * t - t) * crv + t;
        alpha = clamp(alpha, 0.0, 1.0);
    }

    if (pipeID == 0) {
        float top = 1.0 - innerT;
        if (top < 0.001) top = 0.001;
        float mid = innerT;
        alpha = crContFunc(clamp(alpha / top, 0.0, 1.0), mid);
        alpha = clamp(alpha, 0.0, 1.0);

        float fop = crv;
        if (fop <= 0.0) {
            float bpn = crBubFunc(alpha);
            alpha = (bpn - alpha) * abs(fop) + alpha;
        } else {
            float bpn = crPinchFunc(alpha);
            alpha = (bpn - alpha) * fop + alpha;
        }
        alpha = clamp(alpha, 0.0, 1.0);

        if (!(sol >= 0.999 && sol2op <= 0.001)) {
            float noiseVal = hash(uv + floor(time * 100.0));
            float fsol2op = clamp(1.0 - sol2op, 0.0, 1.0);
            float nsal = (noiseVal < sol) ? 1.0 : 0.0;
            float noiseres = (noiseVal < alpha) ? 1.0 : 0.0;
            nsal = noiseres * fsol2op + nsal * (1.0 - fsol2op);
            alpha *= nsal;
        }
    }

    alpha = clamp(alpha, 0.0, 1.0);
    finalColor = vec4(1.0, 1.0, 1.0, alpha);
}
