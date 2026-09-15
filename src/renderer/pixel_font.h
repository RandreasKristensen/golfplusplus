#pragma once

// Bitmap pixel font for the overlay. Each lit glyph pixel becomes one small
// quad appended to an overlay_batch (see renderer/overlay_batch.h) — the chunky
// look comes from these quads, not from a texture or TTF renderer. GL-free.

#include <array>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/overlay_batch.h"

// A lit pixel's quad is slightly smaller than its cell, leaving a visible gap.
constexpr float pixel_glyph_half_size_ratio = 0.42f;

// 7 rows per glyph, '1' = lit. Lowercase is mapped by callers via toupper.
const std::array<const char*, 7>& glyph_rows(char value);
// Width in glyph pixels (space is 3).
int glyph_width(char value);
int glyph_lit_pixel_count(char value);

float pixel_text_width(const std::string& label, float pixel_size);
float fit_pixel_size(const std::string& label,
                     const glm::vec2& max_half_size,
                     float max_pixel_size,
                     float min_pixel_size,
                     float padding = 0.88f);

// Appends one quad per lit pixel of `value`.
void draw_pixel_glyph(overlay_batch& batch,
                      char value,
                      glm::vec2 top_left,
                      float pixel_size,
                      glm::vec3 color);
void draw_pixel_text_centered(overlay_batch& batch,
                              const std::string& label,
                              glm::vec2 center,
                              float pixel_size,
                              glm::vec3 color);
void draw_pixel_text_left(overlay_batch& batch,
                          const std::string& label,
                          glm::vec2 top_left,
                          float pixel_size,
                          glm::vec3 color);
