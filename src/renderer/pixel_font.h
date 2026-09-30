#pragma once

// Draws the bitmap pixel font (game/pixel_font_data.h) into the overlay. Each
// lit glyph pixel becomes one small quad appended to an overlay_batch (see
// renderer/overlay_batch.h): the chunky look comes from these quads, not from
// a texture or TTF renderer. GL-free.

#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/pixel_font_data.h"
#include "game/text_style.h"
#include "renderer/overlay_batch.h"

// A lit pixel's quad is slightly smaller than its cell, leaving a visible gap.
constexpr float pixel_glyph_half_size_ratio = 0.42f;

// Width of `text` with one blank font pixel between glyphs.
float pixel_text_width(const pixel_font_data& font, const std::string& text, float pixel_size);
// Largest pixel size in [min_pixel_size, max_pixel_size] at which `text`
// fits inside max_half_size (scaled by padding).
float fit_pixel_size(const pixel_font_data& font,
                     const std::string& text,
                     const glm::vec2& max_half_size,
                     float max_pixel_size,
                     float min_pixel_size,
                     float padding = 0.88f);

// Appends one quad per lit pixel of `value`.
void draw_pixel_glyph(overlay_batch& batch,
                      const pixel_font_data& font,
                      char value,
                      glm::vec2 top_left,
                      float pixel_size,
                      glm::vec3 color);

// Draws `text` in `style`. The anchor is the text's top-left corner for
// left-aligned styles and its centre for centred ones.
void draw_text(overlay_batch& batch,
               const pixel_font_data& font,
               const text_style& style,
               const std::string& text,
               glm::vec2 anchor);

// The style's pixel size, shrunk (down to its min_pixel_size) so `text` fits
// in max_half_size.
float fitted_pixel_size(const pixel_font_data& font,
                        const text_style& style,
                        const std::string& text,
                        glm::vec2 max_half_size);
// draw_text at fitted_pixel_size.
void draw_text_fitted(overlay_batch& batch,
                      const pixel_font_data& font,
                      const text_style& style,
                      const std::string& text,
                      glm::vec2 anchor,
                      glm::vec2 max_half_size);
