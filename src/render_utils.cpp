#include "render_utils.h"
#include "rlgl.h"
#include "external/glad.h"
#include <cstddef>

unsigned int CreateTexRGBA16(int w, int h) {
    unsigned int id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16, w, h, 0, GL_RGBA, GL_UNSIGNED_SHORT, NULL);
    return id;
}

RenderTexture2D Load16BitRT(int w, int h) {
    RenderTexture2D t={0}; t.id=rlLoadFramebuffer();
    if(t.id>0){ rlEnableFramebuffer(t.id);
        t.texture.id=CreateTexRGBA16(w, h);
        t.texture.width=w; t.texture.height=h; t.texture.format=PIXELFORMAT_UNCOMPRESSED_R16G16B16A16; t.texture.mipmaps=1;
        t.depth.id=rlLoadTextureDepth(w,h,true); t.depth.width=w; t.depth.height=h; t.depth.format=19; t.depth.mipmaps=1;
        rlFramebufferAttach(t.id,t.texture.id,RL_ATTACHMENT_COLOR_CHANNEL0,RL_ATTACHMENT_TEXTURE2D,0);
        rlFramebufferAttach(t.id,t.depth.id,RL_ATTACHMENT_DEPTH,RL_ATTACHMENT_RENDERBUFFER,0);
        rlFramebufferComplete(t.id); rlDisableFramebuffer();
    } return t;
}
