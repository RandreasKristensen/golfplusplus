#pragma once

// A low-resolution render target: the scene (HDR, with depth), the
// camcorder's steps, and the tape pass the TV pass upscales.

enum class framebuffer_color {
    rgb8,      // what the tape records and plays
    hdr,       // R11G11B10F: brighter than white, never negative
    signed_hdr // RGBA16F: any value, alpha too
};

struct framebuffer_format {
    framebuffer_color color = framebuffer_color::rgb8;
    bool depth = true;
    bool linear = false;   // filtered when sampled, else nearest
    bool mipmaps = false;  // allocates the chain for glGenerateMipmap (linear only)
};

class framebuffer {
public:
    bool init(int width, int height, const framebuffer_format& format = framebuffer_format{});
    void shutdown();
    void bind() const;
    static void bind_default();

    unsigned int color_texture() const { return color_tex_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    unsigned int fbo_ = 0;
    unsigned int color_tex_ = 0;
    unsigned int depth_rbo_ = 0;
    int width_ = 0;
    int height_ = 0;
};
