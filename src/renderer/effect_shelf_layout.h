#pragma once

// Where the effect shelf's squares stand (game/effect_shelf.h): small buff-icon
// squares in rows left of the club label, the first row top-aligned with it and
// ending just left of it, each as square on the screen as the UI grid's pixels
// are, the newest leftmost. A full row continues in a row below it, right-aligned
// to the same edge, two rows spanning the club label box's height. The one layout
// the drawing and the hit test both use. When there are more sources than fit
// before the mode label, the last slot is a `+N` square. GL-free.

#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>

// In UI grid pixels.
inline constexpr int effect_square_pixels = 12;   // a square's side, its border included
inline constexpr int effect_square_gap_pixels = 2;
inline constexpr int effect_square_border_pixels = 1;
// The picture inside the border.
inline constexpr int effect_icon_pixels = effect_square_pixels - 2 * effect_square_border_pixels;

struct effect_shelf_layout {
    // Newest first (row by row, leftmost first); with `hidden`, the last is the `+N` square.
    std::vector<ui_rect> squares;
    // Under each square, where its seconds go; the `+N` has none to show.
    std::vector<ui_rect> seconds_boxes;
    std::size_t per_row = 0;  // squares in a full row
    std::size_t rows = 0;     // always two
    std::size_t shown = 0;   // sources with a square of their own
    std::size_t hidden = 0;  // sources behind the `+N` square
};

// The slots for `source_count` sources, newest first, on `grid`.
effect_shelf_layout layout_effect_shelf(std::size_t source_count, const overlay_grid& grid);

// The source whose square holds `point` (overlay clip space); none over the
// `+N` square or off the shelf.
std::optional<std::size_t> effect_square_at(const effect_shelf_layout& layout, glm::vec2 point);

// `size_px` UI grid pixels as a size in overlay clip space.
glm::vec2 effect_pixels(const overlay_grid& grid, float size_px);
