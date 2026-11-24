#include "renderer/effect_shelf_layout.h"

#include "renderer/hud_boxes.h"

#include <algorithm>
#include <cmath>

namespace {
// Clear of the club label and of the mode label's box.
constexpr int club_gap_pixels = 3;
constexpr int mode_label_gap_pixels = 6;
// The seconds' box under a square.
constexpr int seconds_gap_pixels = 1;
constexpr int seconds_height_pixels = 8;
// The two rows together span the club label's height: row 1's square top is
// level with its top, row 2's seconds bottom with its bottom.
constexpr std::size_t shelf_rows = 2;

bool contains(const ui_rect& rect, const glm::vec2 point) {
    return point.x >= rect_left(rect) && point.x <= rect_right(rect) && point.y >= rect_bottom(rect) &&
        point.y <= rect_top(rect);
}
}

glm::vec2 effect_pixels(const overlay_grid& grid, const float size_px) {
    return glm::vec2(size_px * 2.0f / static_cast<float>(grid.width), size_px * 2.0f / static_cast<float>(grid.height));
}

effect_shelf_layout layout_effect_shelf(const std::size_t source_count, const overlay_grid& grid) {
    effect_shelf_layout layout;
    if (grid.width <= 0 || grid.height <= 0 || source_count == 0) {
        return layout;
    }
    const glm::vec2 one = effect_pixels(grid, 1.0f);
    const float slot = static_cast<float>(effect_square_pixels + effect_square_gap_pixels) * one.x;
    const float row_height =
        static_cast<float>(effect_square_pixels + seconds_gap_pixels + seconds_height_pixels) * one.y;
    const ui_rect club = club_label_box();
    const float right = rect_left(club) - static_cast<float>(club_gap_pixels) * one.x;
    const float left_limit = rect_right(mode_label_box()) + static_cast<float>(mode_label_gap_pixels) * one.x;
    layout.per_row = static_cast<std::size_t>(
        std::max(0.0f, std::floor((right - left_limit + static_cast<float>(effect_square_gap_pixels) * one.x) / slot)));
    layout.rows = shelf_rows;
    const std::size_t capacity = layout.per_row * layout.rows;

    if (capacity == 0) {
        layout.hidden = source_count;
        return layout;
    }
    layout.shown = source_count <= capacity ? source_count : capacity - 1;
    layout.hidden = source_count - layout.shown;
    const std::size_t slots = layout.shown + (layout.hidden > 0 ? 1 : 0);

    const glm::vec2 side = effect_pixels(grid, static_cast<float>(effect_square_pixels));
    for (std::size_t i = 0; i < slots; ++i) {
        const std::size_t row = i / layout.per_row;
        const std::size_t in_row = std::min(layout.per_row, slots - row * layout.per_row);
        const std::size_t column = i % layout.per_row;
        const float square_right = right - static_cast<float>(in_row - 1 - column) * slot;
        // Row 1 hangs from the club's top, the last row stands on its bottom.
        const float square_top = row == 0 ? rect_top(club) : rect_bottom(club) + row_height;
        const ui_rect square = rect_from_edges(square_right - side.x, square_top - side.y, square_right, square_top);
        layout.squares.push_back(square);
        const float seconds_top = rect_bottom(square) - static_cast<float>(seconds_gap_pixels) * one.y;
        layout.seconds_boxes.push_back(rect_from_edges(square.center.x - slot * 0.5f,
                                                       seconds_top - static_cast<float>(seconds_height_pixels) * one.y,
                                                       square.center.x + slot * 0.5f, seconds_top));
    }
    return layout;
}

std::optional<std::size_t> effect_square_at(const effect_shelf_layout& layout, const glm::vec2 point) {
    for (std::size_t i = 0; i < layout.shown && i < layout.squares.size(); ++i) {
        if (contains(layout.squares[i], point)) {
            return i;
        }
    }
    return std::nullopt;
}
