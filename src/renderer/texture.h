#pragma once

// One RGBA image uploaded as an OpenGL 2D texture.

#include "renderer/gl_storage.h"
#include "renderer/bmp_image.h"

// How a texture is sampled.
enum class texture_sampling {
    // Ground detail: chunky texels up close, mipmaps fading it to its average
    // colour in the distance instead of shimmering. Repeats both ways.
    tiled_detail,
    // A panorama: smooth up close, repeating around the horizon and clamped
    // at the top and bottom.
    panorama,
    // A picture of whole texels: never blended between, never repeated.
    texels
};

class texture {
public:
    explicit texture(gl_storage storage = {})
        : storage_(storage) {}

    // Replaces any texture this held. False if `image` is empty.
    bool upload(const rgba_image& image, texture_sampling sampling);
    void shutdown();
    void bind(int unit) const;
    std::uint64_t logical_bytes() const { return storage_.ledger ? storage_.ledger->textures.bytes(id_) : 0; }
    bool loaded() const { return id_ != 0; }

private:
    gl_storage storage_;
    unsigned int id_ = 0;
};
