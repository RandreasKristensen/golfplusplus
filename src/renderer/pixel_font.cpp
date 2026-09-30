#include "renderer/pixel_font.h"

#include <algorithm>

float pixel_text_width(const pixel_font_data& font, const std::string& text, const float pixel_size) {
    float width = 0.0f;
    for (const char c : text) {
        width += static_cast<float>(find_glyph(font, c).width + 1) * pixel_size;
    }
    return std::max(0.0f, width - pixel_size);
}

float fit_pixel_size(const pixel_font_data& font,
                     const std::string& text,
                     const glm::vec2& max_half_size,
                     const float max_pixel_size,
                     const float min_pixel_size,
                     const float padding) {
    if (text.empty()) {
        return min_pixel_size;
    }

    const float max_width = max_half_size.x * 2.0f * padding;
    const float max_height = max_half_size.y * 2.0f * padding;
    const float base_width = std::max(1.0f, pixel_text_width(font, text, 1.0f));
    const float width_scale = max_width / base_width;
    const float height_scale = max_height / static_cast<float>(font.height);
    const float target = std::min(width_scale, height_scale);
    return std::clamp(target, min_pixel_size, max_pixel_size);
}

void draw_pixel_glyph(overlay_batch& batch,
                      const pixel_font_data& font,
                      const char value,
                      const glm::vec2 top_left,
                      const float pixel_size,
                      const glm::vec3 color) {
    const pixel_glyph& glyph = find_glyph(font, value);
    if (glyph.lit_pixel_count == 0) {
        return;
    }

    for (int y = 0; y < font.height; ++y) {
        for (int x = 0; x < glyph.width; ++x) {
            if (glyph.pixels[static_cast<std::size_t>(y * glyph.width + x)] == 0U) {
                continue;
            }

            draw_overlay_quad(batch,
                              top_left + glm::vec2((static_cast<float>(x) + 0.5f) * pixel_size,
                                                   -(static_cast<float>(y) + 0.5f) * pixel_size),
                              glm::vec2(pixel_size * pixel_glyph_half_size_ratio),
                              color);
        }
    }
}

void draw_text(overlay_batch& batch,
               const pixel_font_data& font,
               const text_style& style,
               const std::string& text,
               const glm::vec2 anchor) {
    const float pixel_size = style.pixel_size;
    glm::vec2 cursor = anchor;
    if (style.align == text_align::center) {
        cursor = glm::vec2(anchor.x - pixel_text_width(font, text, pixel_size) * 0.5f,
                           anchor.y + static_cast<float>(font.height) * 0.5f * pixel_size);
    }

    for (const char c : text) {
        draw_pixel_glyph(batch, font, c, cursor, pixel_size, style.color);
        cursor.x += static_cast<float>(find_glyph(font, c).width + 1) * pixel_size;
    }
}

float fitted_pixel_size(const pixel_font_data& font,
                        const text_style& style,
                        const std::string& text,
                        const glm::vec2 max_half_size) {
    return fit_pixel_size(font, text, max_half_size, style.pixel_size, style.min_pixel_size);
}

void draw_text_fitted(overlay_batch& batch,
                      const pixel_font_data& font,
                      const text_style& style,
                      const std::string& text,
                      const glm::vec2 anchor,
                      const glm::vec2 max_half_size) {
    draw_text(batch, font, with_pixel_size(style, fitted_pixel_size(font, style, text, max_half_size)), text, anchor);
}
