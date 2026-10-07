#include "renderer/overlay_raster.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <utility>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace {
std::uint8_t to_byte(const float value) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
}

float edge_function(const glm::vec2 a, const glm::vec2 b, const glm::vec2 p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

// For a counter-clockwise triangle (y up): the edge a -> b is a top edge
// (horizontal, running left) or a left edge (running down).
bool top_left_edge(const glm::vec2 a, const glm::vec2 b) {
    const glm::vec2 d = b - a;
    return (d.y == 0.0f && d.x < 0.0f) || d.y < 0.0f;
}

bool covers(const float w, const bool top_left) {
    return w > 0.0f || (w == 0.0f && top_left);
}

void blend(rgba_image& image, const int column, const int row, const glm::vec4& color) {
    std::uint8_t* pixel = &image.pixels[(static_cast<std::size_t>(row) * static_cast<std::size_t>(image.width) +
                                         static_cast<std::size_t>(column)) * 4];
    const float alpha = std::clamp(color.a, 0.0f, 1.0f);
    for (int channel = 0; channel < 3; ++channel) {
        const float below = static_cast<float>(pixel[channel]) / 255.0f;
        pixel[channel] = to_byte(below + (color[channel] - below) * alpha);
    }
}

// `a`, `b`, `c` in texel units (x right, y up), any winding.
void paint_triangle(rgba_image& image, glm::vec2 a, glm::vec2 b, glm::vec2 c, const glm::vec4& color) {
    if (edge_function(a, b, c) < 0.0f) {
        std::swap(b, c);
    }
    if (edge_function(a, b, c) == 0.0f) {
        return;
    }
    const bool top_left_ab = top_left_edge(a, b);
    const bool top_left_bc = top_left_edge(b, c);
    const bool top_left_ca = top_left_edge(c, a);

    const int first_column = std::max(0, static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))));
    const int last_column = std::min(image.width - 1, static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))));
    const int first_row = std::max(0, static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))));
    const int last_row = std::min(image.height - 1, static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))));
    for (int row = first_row; row <= last_row; ++row) {
        for (int column = first_column; column <= last_column; ++column) {
            const glm::vec2 p(static_cast<float>(column) + 0.5f, static_cast<float>(row) + 0.5f);
            if (covers(edge_function(b, c, p), top_left_bc) &&
                covers(edge_function(c, a, p), top_left_ca) &&
                covers(edge_function(a, b, p), top_left_ab)) {
                blend(image, column, row, color);
            }
        }
    }
}
}

rgba_image rasterize_overlay_batch(const overlay_batch& batch, const glm::vec3 background) {
    rgba_image image;
    image.width = std::max(0, batch.grid.width);
    image.height = std::max(0, batch.grid.height);
    image.pixels.resize(static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i] = to_byte(background.r);
        image.pixels[i + 1] = to_byte(background.g);
        image.pixels[i + 2] = to_byte(background.b);
        image.pixels[i + 3] = 255;
    }
    if (image.width == 0 || image.height == 0) {
        return image;
    }

    // Clip space [-1, 1] -> texels [0, size].
    const glm::vec2 size(static_cast<float>(image.width), static_cast<float>(image.height));
    const auto to_texels = [&size](const glm::vec2 clip) { return (clip + 1.0f) * 0.5f * size; };
    for (std::size_t i = 0; i + 2 < batch.vertices.size(); i += 3) {
        paint_triangle(image,
                       to_texels(batch.vertices[i].position),
                       to_texels(batch.vertices[i + 1].position),
                       to_texels(batch.vertices[i + 2].position),
                       batch.vertices[i].color);
    }
    return image;
}
