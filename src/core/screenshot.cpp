#include "core/screenshot.h"

#include "game/content_files.h"
#include "renderer/bmp_image.h"
#include "renderer/framebuffer.h"
#include "renderer/gl_loader.h"

#include <algorithm>
#include <cstddef>

bool save_screenshot(SDL_Window* window, const std::filesystem::path& path) {
    rgba_image image;
    SDL_GL_GetDrawableSize(window, &image.width, &image.height);
    image.width = std::max(1, image.width);
    image.height = std::max(1, image.height);
    image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4U);
    framebuffer::bind_default();
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    // Rows come bottom up, as rgba_image keeps them.
    glReadPixels(0, 0, image.width, image.height, GL_RGBA, GL_UNSIGNED_BYTE, image.pixels.data());
    return replace_text_file(path, encode_bmp(image));
}
