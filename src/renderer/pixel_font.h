#pragma once

// Overlay text in the bitmap pixel font (game/pixel_font_data.h). Text is
// always drawn into a box: its style says how much of the box it fills, and
// the layout picks the largest whole-number scale that fits, so every font
// pixel is a square block of target pixels (overlay_batch::grid) at any
// window size or aspect ratio. Text too long for its box wraps between words
// (if the style allows), shrinks down to the style's min_scale, and is then
// cut off with the font's ellipsis: it never draws outside its box. Each lit
// font pixel becomes one quad in the overlay batch; the chunky look comes
// from these quads, not a texture or TTF renderer. Text is UTF-8. GL-free.

#include <string>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/pixel_font_data.h"
#include "game/text_style.h"
#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

struct text_line {
    std::u32string glyphs;
    glm::vec2 top_left = glm::vec2(0.0f);  // overlay clip space, on the pixel grid
};

struct text_layout {
    int scale = 0;                      // target pixels per font pixel
    glm::vec2 cell = glm::vec2(0.0f);  // one font pixel in clip units
    std::vector<text_line> lines;
    ui_rect bounds;                     // what the lines cover
    bool truncated = false;             // cut off with the ellipsis
};

// Largest scale at which all of `text` fits `box`: at most the style's fill
// (rounded) and max_scale, at least its min_scale. 0 when it doesn't fit
// even at min_scale. Use it to give several labels one shared size.
int fit_text_scale(const pixel_font_data& font,
                   const text_style& style,
                   const std::string& text,
                   const ui_rect& box,
                   const overlay_grid& grid);
// `text` at `scale`, with lines that don't fit dropped and the last one cut
// off with the ellipsis.
text_layout layout_text_at_scale(const pixel_font_data& font,
                                 const text_style& style,
                                 const std::string& text,
                                 const ui_rect& box,
                                 const overlay_grid& grid,
                                 int scale);
// At fit_text_scale, or cut off at min_scale when nothing fits.
text_layout layout_text(const pixel_font_data& font,
                        const text_style& style,
                        const std::string& text,
                        const ui_rect& box,
                        const overlay_grid& grid);

// Appends one quad per lit font pixel; a truncated layout also counts in
// batch.truncated_text_count.
void draw_text_layout(overlay_batch& batch, const pixel_font_data& font, const text_layout& layout, glm::vec3 color);
// layout_text on the batch's grid, in the style's colour. The usual way to
// draw any overlay text.
void draw_label(overlay_batch& batch,
                const pixel_font_data& font,
                const text_style& style,
                const std::string& text,
                const ui_rect& box);
