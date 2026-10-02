#pragma once

// One RGB image uploaded as an OpenGL 2D texture.

#include "renderer/bmp_image.h"

// How a texture is sampled.
enum class texture_sampling {
    // Ground detail: chunky texels up close, mipmaps fading it to its average
    // colour in the distance instead of shimmering. Repeats both ways.
    tiled_detail,
    // A panorama: smooth up close, repeating around the horizon and clamped
    // at the top and bottom.
    panorama
};

class texture {
public:
    // Replaces any texture this held. False if `image` is empty.
    bool upload(const rgb_image& image, texture_sampling sampling);
    void shutdown();
    void bind(int unit) const;
    bool loaded() const { return id_ != 0; }

private:
    unsigned int id_ = 0;
};
