#version 330
//brush blend vs
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fragTexCoord;
out vec2 canvasFragUV;
out vec2 outCanvasPx;

uniform mat4 mvp;
uniform vec2 stampOffset;
uniform vec2 canvasSize;
uniform float radOut;

void main() {
    fragTexCoord = vertexTexCoord;

    float bboxSize = radOut * 1.41421356 * 2.0;

    // vertexTexCoord.y=0 is bottom of quad in RT = canvas y0+bboxSize
    // vertexTexCoord.y=1 is top of quad in RT   = canvas y0
    vec2 canvasPx;
    canvasPx.x = stampOffset.x + vertexTexCoord.x * bboxSize;
    canvasPx.y = stampOffset.y + (1.0 - vertexTexCoord.y) * bboxSize;

    // GL texture sample: y=0 bottom, so flip
    canvasFragUV = vec2(canvasPx.x / canvasSize.x,
                        1.0 - canvasPx.y / canvasSize.y);
    outCanvasPx = canvasPx;

    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
