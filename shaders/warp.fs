#version 330
in vec2 fragTexCoord;
out vec4 finalColor;

uniform sampler2D srcTex;
uniform vec2 srcSize;
uniform vec2 dstSize;
uniform vec3 invH_row0;
uniform vec3 invH_row1;
uniform vec3 invH_row2;

void main() {
    vec2 dstPx = fragTexCoord * dstSize;
    vec3 src = vec3(
        dot(invH_row0, vec3(dstPx, 1.0)),
        dot(invH_row1, vec3(dstPx, 1.0)),
        dot(invH_row2, vec3(dstPx, 1.0))
    );
    vec2 srcUV = src.xy / src.z;
    if (srcUV.x < 0.0 || srcUV.x > srcSize.x ||
        srcUV.y < 0.0 || srcUV.y > srcSize.y) {
        finalColor = vec4(0.0);
        return;
    }
    vec2 uv = srcUV / srcSize;
    uv.y = 1.0 - uv.y;
    finalColor = texture(srcTex, uv);
}
