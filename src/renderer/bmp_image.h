#pragma once

// Reads the uncompressed 24- and 32-bit BMP files in assets/ (course
// backdrops, ground textures) into RGBA pixels. GL-free, so tests check every
// shipped image decodes; the renderer uploads the result as a texture.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// 8-bit RGBA, rows from the bottom of the picture up (OpenGL's texture order),
// so pixels[(row * width + column) * 4] is the red of that pixel. Alpha is
// straight (not premultiplied).
struct rgba_image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Nothing for anything but an uncompressed 24- or 32-bit BMP. Alpha is read
// only from a 32-bit BI_BITFIELDS file whose header has an alpha mask
// (BITMAPV4HEADER or later, as tooling/art/make_art.py writes); every other
// pixel is opaque.
std::optional<rgba_image> parse_bmp(const std::string& bytes);
std::optional<rgba_image> load_bmp_file(const std::filesystem::path& path);
