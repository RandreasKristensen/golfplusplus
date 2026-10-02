#pragma once

// Reads the uncompressed 24- and 32-bit BMP files in assets/ (course
// backdrops, ground textures) into RGB pixels. GL-free, so tests check every
// shipped image decodes; the renderer uploads the result as a texture.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// 8-bit RGB, rows from the bottom of the picture up (OpenGL's texture order),
// so pixels[(row * width + column) * 3] is the red of that pixel.
struct rgb_image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

// Nothing for anything but an uncompressed 24- or 32-bit BMP (alpha dropped).
std::optional<rgb_image> parse_bmp(const std::string& bytes);
std::optional<rgb_image> load_bmp_file(const std::filesystem::path& path);
