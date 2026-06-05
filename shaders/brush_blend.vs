#version 330
//brush blend vs
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fragTexCoord;
out vec2 canvasFragUV;
out vec2 outCanvasPx;

uniform mat4 mvp;
uniform vec2 blitOrigin;  // floor(x0), floor(y0) — pixel-aligned stamp top-left
uniform vec2 blitSize;    // floor(stampSizePx) — pixel-aligned stamp size
uniform vec2 fracShift;   // x0-floor(x0), y0-floor(y0) — sub-pixel offset [0,1)
uniform vec2 canvasSize;

void main() {
    fragTexCoord = vertexTexCoord;

    // outCanvasPx: exact canvas pixel position for each fragment
    // Integer part = correct canvas pixel. Fractional = sub-pixel detail
    vec2 tp = vec2(vertexTexCoord.x, 1.0 - vertexTexCoord.y);
    //outCanvasPx = blitOrigin + fracShift + tp * blitSize; //  opencode check this.
    outCanvasPx = blitOrigin +  tp * blitSize;+

    // Normalized for texture noise / other uses
    canvasFragUV = vec2(outCanvasPx.x / canvasSize.x,
                        1.0 - outCanvasPx.y / canvasSize.y);

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
