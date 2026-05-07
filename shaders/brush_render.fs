#version 330

// Brush rendering shader - applies brush stamp to canvas
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D canvasTex;
uniform sampler2D brushTex;
uniform vec2 brushPos;
uniform float opacity;
uniform int blendMode;

void main() {
    vec4 canvasColor = texture(canvasTex, fragTexCoord);
    vec2 brushUV = (fragTexCoord - brushPos) + vec2(0.5);
    vec4 brushColor = texture(brushTex, brushUV);

    // Apply blend mode (simplified from bmBlends enum)
    vec4 result;
    if (blendMode == 0) { // bmNormal
        result = mix(canvasColor, brushColor, brushColor.a * opacity);
    } else if (blendMode == 1) { // bmPlus
        result = canvasColor + brushColor * brushColor.a * opacity;
    } else if (blendMode == 2) { // bmDodge
        result = canvasColor + (brushColor * brushColor.a * opacity) * (1.0 - canvasColor);
    } else if (blendMode == 3) { // bmScreen
        result = 1.0 - (1.0 - canvasColor) * (1.0 - brushColor * brushColor.a * opacity);
    } else if (blendMode == 4) { // bmLighten
        result = max(canvasColor, brushColor * brushColor.a * opacity);
    } else if (blendMode == 5) { // bmBurn
        result = 1.0 - (1.0 - canvasColor) / (brushColor * brushColor.a * opacity + 0.001);
    } else if (blendMode == 6) { // bmMult
        result = canvasColor * mix(vec4(1.0), brushColor, brushColor.a * opacity);
    } else if (blendMode == 7) { // bmDarken
        result = min(canvasColor, brushColor * brushColor.a * opacity);
    } else {
        result = mix(canvasColor, brushColor, brushColor.a * opacity);
    }

    finalColor = result;
}
