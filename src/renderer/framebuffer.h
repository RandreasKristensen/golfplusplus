#pragma once

struct framebuffer {
    bool init(int width, int height);
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
