#include "renderer/framebuffer.h"

#include <SDL.h>

#include "renderer/gl_loader.h"

bool framebuffer::init(const int width, const int height, const framebuffer_format& format) {
    shutdown();

    width_ = width;
    height_ = height;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    glGenTextures(1, &color_tex_);
    glBindTexture(GL_TEXTURE_2D, color_tex_);
    const bool mipmaps = format.linear && format.mipmaps;
    switch (format.color) {
    case framebuffer_color::rgb8:
        storage_.texture_image(color_tex_, GL_RGB, width_, height_, GL_RGB, GL_UNSIGNED_BYTE, nullptr, mipmaps);
        break;
    case framebuffer_color::hdr:
        storage_.texture_image(color_tex_, GL_R11F_G11F_B10F, width_, height_, GL_RGB, GL_FLOAT, nullptr, mipmaps);
        break;
    case framebuffer_color::signed_hdr:
        storage_.texture_image(color_tex_, GL_RGBA16F, width_, height_, GL_RGBA, GL_FLOAT, nullptr, mipmaps);
        break;
    case framebuffer_color::exact:
        storage_.texture_image(color_tex_, GL_RGBA32F, width_, height_, GL_RGBA, GL_FLOAT, nullptr, mipmaps);
        break;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmaps ? GL_LINEAR_MIPMAP_NEAREST : format.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, format.linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);

    if (format.depth == framebuffer_depth::drawn) {
        glGenRenderbuffers(1, &depth_rbo_);
        glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
        storage_.depth_stencil(depth_rbo_, width_, height_);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);
    } else if (format.depth == framebuffer_depth::sampled) {
        glGenTextures(1, &depth_tex_);
        glBindTexture(GL_TEXTURE_2D, depth_tex_);
        storage_.texture_image(depth_tex_, GL_DEPTH24_STENCIL8, width_, height_, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, depth_tex_, 0);
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        SDL_Log("A %dx%d framebuffer is incomplete.", width_, height_);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        shutdown();
        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void framebuffer::shutdown() {
    storage_.delete_renderbuffer(depth_rbo_);

    storage_.delete_texture(depth_tex_);

    storage_.delete_texture(color_tex_);

    if (fbo_ != 0) {
        glDeleteFramebuffers(1, &fbo_);
        fbo_ = 0;
    }

    width_ = 0;
    height_ = 0;
}

void framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
}

void framebuffer::bind_default() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
