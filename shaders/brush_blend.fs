#version 330

in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D texture0;   // geo UV pass (intermediate_rt)
uniform sampler2D canvasTex;  // canvas copy
uniform sampler2D brushTex;   // user brush texture
uniform float opacity;
uniform float radIn;
uniform float useTex;
uniform vec3 brushRGB;
uniform float curve;

void main() {
    vec2 uv = fragTexCoord;
    vec2 sampleUV = vec2(uv.x, 1.0 - uv.y);

    vec4 canvas  = texture(canvasTex, uv);
    vec4 geouv = texture(texture0, sampleUV);

if(false){ // debug
finalColor = geouv;
finalColor.a=1;
return;
}

    if (geouv.a < 0.01) {
        finalColor = canvas;
        return;
    }
//if we have texture we need some steps to properly get alpha
// call it userTexA;
// if use lum - then get it from lum
// if not - then from tex.a

// if tex treshold<1 then invert it : userTexA = 1-userTexA

// if use texture as mask firstMask = userTexA, secondmask =1;
// else firstmask = linear gradientfrom center to side. secondmask = userTexA
// apply radIn and crv to first mask.

// if trehsold mode - multiply both masks, and then apply abs(treshold and feather)
// if mask mode - multiply masks.finalColor
// if use tex mask - return first mask


    vec2 p = geouv.rg - 0.5;
    float dist = length(p);
    float inner = clamp(radIn, 0.0, 0.99);
    float circle = smoothstep(0.5, inner * 0.5, dist);
    circle = pow(circle, curve * 3.0 + 1.0);

    vec3 col;
    if (useTex > 0.5) {
        vec4 t = texture(brushTex, geouv.rg);
        col = t.rgb;
    } else {
        col = brushRGB;
    }

    float a = circle * opacity;
    finalColor.rgb = col * a + canvas.rgb * (1.0 - a);
    finalColor.a   = a + canvas.a * (1.0 - a);
}
