#include "renderer/texture.h"

#include "renderer/gl_loader.h"

bool texture::upload(const rgba_image& image, const texture_sampling sampling) {
    shutdown();
    if (image.width <= 0 || image.height <= 0 || image.pixels.empty()) {
        return false;
    }
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    storage_.texture_image(id_, GL_RGBA, image.width, image.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data(), sampling == texture_sampling::tiled_detail);
    if (sampling == texture_sampling::tiled_detail) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    } else if (sampling == texture_sampling::texels) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void texture::shutdown() {
    storage_.delete_texture(id_);
}

void texture::bind(const int unit) const {
    glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + unit));
    glBindTexture(GL_TEXTURE_2D, id_);
    // Code that binds without choosing a unit expects unit 0.
    glActiveTexture(GL_TEXTURE0);
}
