#include "renderer/effect_shelf_overlay.h"

#include "game/text_ids.h"
#include "renderer/effect_icons.h"
#include "renderer/effect_shelf_layout.h"
#include "renderer/hud_panel.h"

#include <algorithm>
#include <cstddef>
#include <string>

#include <glm/common.hpp>
#include <glm/vec3.hpp>

namespace {
// The tooltip, in UI grid pixels.
constexpr float tooltip_padding = 4.0f;
constexpr float tooltip_text_width = 184.0f;
constexpr float tooltip_section_gap = 3.0f;
constexpr float tooltip_offset_x = 10.0f;
constexpr float tooltip_offset_y = 14.0f;
constexpr float tooltip_screen_margin = 4.0f;
constexpr int tooltip_text_scale = 1;
// A text box a hair taller than its lines, so rounding in the clip-space round trip never cuts one off.
constexpr float text_box_slack = 0.5f;

constexpr float face_alpha = 0.86f;
constexpr float hovered_glow_alpha = 0.22f;
constexpr float tooltip_alpha = 0.92f;
constexpr float shadow_offset_pixels = 1.0f;
const glm::vec3 shadow_colour(0.04f, 0.035f, 0.03f);

// A box in UI pixels (left, top from the screen's top-left) as overlay clip space.
ui_rect pixel_box(const overlay_grid& grid, const float left, const float top, const float width, const float height) {
    const float w = static_cast<float>(grid.width);
    const float h = static_cast<float>(grid.height);
    return rect_from_edges(left * 2.0f / w - 1.0f, 1.0f - (top + height) * 2.0f / h, (left + width) * 2.0f / w - 1.0f,
                           1.0f - top * 2.0f / h);
}

text_style whole_width(text_style style) {
    style.width_fill = 1.0f;
    return style;
}

// The border: four edges of `thickness` inside `box`.
void draw_border(overlay_batch& batch, const ui_rect& box, const glm::vec2 thickness, const glm::vec3 colour) {
    const glm::vec2 half = box.half_size;
    const glm::vec2 across(half.x, thickness.y * 0.5f);
    const glm::vec2 down(thickness.x * 0.5f, half.y);
    draw_overlay_quad(batch, box.center + glm::vec2(0.0f, half.y - across.y), across, colour);
    draw_overlay_quad(batch, box.center - glm::vec2(0.0f, half.y - across.y), across, colour);
    draw_overlay_quad(batch, box.center + glm::vec2(half.x - down.x, 0.0f), down, colour);
    draw_overlay_quad(batch, box.center - glm::vec2(half.x - down.x, 0.0f), down, colour);
}

// Text with a dark copy a pixel below and right, so it reads over any scene.
void draw_shadowed_label(overlay_batch& batch, const text_assets& text, const text_style& style, const std::string& label,
                         const ui_rect& box) {
    const glm::vec2 shadow = effect_pixels(batch.grid, shadow_offset_pixels);
    draw_label(batch, text.font, with_color(style, shadow_colour), label,
               ui_rect{box.center + glm::vec2(shadow.x, -shadow.y), box.half_size});
    draw_label(batch, text.font, style, label, box);
}

void draw_square(overlay_batch& batch, const text_assets& text, const render_effect_square& square, const ui_rect& box,
                 const ui_rect& seconds_box, const bool hovered) {
    const glm::vec3 border = square.harmful ? find_text_style(text, style_error).color : hud_amber;
    draw_overlay_quad(batch, box.center, box.half_size, hud_panel_color, face_alpha);
    if (hovered) {
        draw_overlay_quad(batch, box.center, box.half_size, hud_outline_color, hovered_glow_alpha);
    }
    draw_border(batch, box, effect_pixels(batch.grid, static_cast<float>(effect_square_border_pixels)), border);
    draw_effect_icon(batch, square.icon,
                     ui_rect{box.center, effect_pixels(batch.grid, static_cast<float>(effect_icon_pixels) * 0.5f)});
    if (!square.seconds.empty()) {
        draw_shadowed_label(batch, text, find_text_style(text, style_effect_seconds), square.seconds, seconds_box);
    }
}

void draw_more_square(overlay_batch& batch, const text_assets& text, const std::size_t hidden, const ui_rect& box) {
    draw_overlay_quad(batch, box.center, box.half_size, hud_panel_color, face_alpha);
    draw_border(batch, box, effect_pixels(batch.grid, static_cast<float>(effect_square_border_pixels)), hud_outline_color);
    // Only a digit fits beside the plus; more than that reads "9+".
    const std::string label = hidden < 10 ? format_text(text, text_hud_effect_more, {{"count", std::to_string(hidden)}})
                                          : lookup_text(text, text_hud_effect_more_many);
    draw_label(batch, text.font, find_text_style(text, style_effect_seconds), label,
               inset_rect(box, effect_pixels(batch.grid, static_cast<float>(effect_square_border_pixels))));
}

std::optional<std::size_t> hovered_square(const effect_shelf_layout& layout, const std::optional<glm::vec2>& cursor) {
    return cursor ? effect_square_at(layout, *cursor) : std::nullopt;
}

// The height of `label` wrapped to `width` pixels, in pixels; zero when empty.
float wrapped_height_pixels(const text_assets& text, const text_style& style, const std::string& label, const float width,
                            const overlay_grid& grid) {
    const text_layout layout =
        layout_text_at_scale(text.font, style, label, pixel_box(grid, 0.0f, 0.0f, width, 1000.0f), grid, tooltip_text_scale);
    const int lines = static_cast<int>(layout.lines.size());
    if (lines == 0) {
        return 0.0f;
    }
    return static_cast<float>((lines * text.font.height + (lines - 1) * text.font.line_gap) * tooltip_text_scale);
}
}

void draw_effect_shelf(overlay_batch& batch, const text_assets& text, const std::vector<render_effect_square>& squares,
                       const std::optional<glm::vec2>& cursor) {
    const effect_shelf_layout layout = layout_effect_shelf(squares.size(), batch.grid);
    const std::optional<std::size_t> hovered = hovered_square(layout, cursor);
    for (std::size_t i = 0; i < layout.squares.size(); ++i) {
        if (i < layout.shown) {
            draw_square(batch, text, squares[i], layout.squares[i], layout.seconds_boxes[i], hovered == i);
        } else {
            draw_more_square(batch, text, layout.hidden, layout.squares[i]);
        }
    }
}

effect_tooltip layout_effect_tooltip(const text_assets& text, const render_effect_square& square, const ui_rect& hovered,
                                     const glm::vec2 cursor, const overlay_grid& grid) {
    const text_style name_style = whole_width(find_text_style(text, style_heading));
    const text_style description_style = whole_width(find_text_style(text, style_tile_label_small));
    const text_style change_style = whole_width(find_text_style(text, style_card_detail));
    const float line = static_cast<float>(text.font.height * tooltip_text_scale);
    const float line_gap = static_cast<float>(text.font.line_gap * tooltip_text_scale);

    const float description_height = wrapped_height_pixels(text, description_style, square.description, tooltip_text_width, grid);
    float inner_height = line;
    if (description_height > 0.0f) {
        inner_height += tooltip_section_gap + description_height;
    }
    for (std::size_t i = 0; i < square.changes.size(); ++i) {
        inner_height += (i == 0 ? tooltip_section_gap : line_gap) + line;
    }
    const float width = tooltip_text_width + tooltip_padding * 2.0f;
    const float height = inner_height + tooltip_padding * 2.0f;

    // Down and right of the cursor, never above the hovered square's bottom edge, then inside the screen.
    const float grid_w = static_cast<float>(grid.width);
    const float grid_h = static_cast<float>(grid.height);
    const float cursor_x = (cursor.x + 1.0f) * 0.5f * grid_w;
    const float cursor_y = (1.0f - cursor.y) * 0.5f * grid_h;
    const float below_square = (1.0f - rect_bottom(hovered)) * 0.5f * grid_h + tooltip_section_gap;
    const float left =
        std::clamp(cursor_x + tooltip_offset_x, tooltip_screen_margin, std::max(tooltip_screen_margin, grid_w - tooltip_screen_margin - width));
    const float top = std::min(std::max(cursor_y + tooltip_offset_y, below_square), std::max(below_square, grid_h - tooltip_screen_margin - height));

    effect_tooltip tooltip;
    tooltip.panel = pixel_box(grid, left, top, width, height);
    const float x = left + tooltip_padding;
    float y = top + tooltip_padding;
    tooltip.name = layout_text_at_scale(text.font, name_style, square.name, pixel_box(grid, x, y, tooltip_text_width, line + text_box_slack), grid,
                                        tooltip_text_scale);
    y += line;
    if (description_height > 0.0f) {
        y += tooltip_section_gap;
        tooltip.description = layout_text_at_scale(text.font, description_style, square.description,
                                                   pixel_box(grid, x, y, tooltip_text_width, description_height + text_box_slack), grid,
                                                   tooltip_text_scale);
        y += description_height;
    }
    for (std::size_t i = 0; i < square.changes.size(); ++i) {
        y += i == 0 ? tooltip_section_gap : line_gap;
        tooltip.changes.push_back(layout_text_at_scale(text.font, change_style, square.changes[i],
                                                       pixel_box(grid, x, y, tooltip_text_width, line + text_box_slack), grid, tooltip_text_scale));
        y += line;
    }
    return tooltip;
}

void draw_effect_tooltip(overlay_batch& batch, const text_assets& text, const std::vector<render_effect_square>& squares,
                         const std::optional<glm::vec2>& cursor) {
    const effect_shelf_layout layout = layout_effect_shelf(squares.size(), batch.grid);
    const std::optional<std::size_t> hovered = hovered_square(layout, cursor);
    if (!hovered) {
        return;
    }
    const effect_tooltip tooltip = layout_effect_tooltip(text, squares[*hovered], layout.squares[*hovered], *cursor, batch.grid);
    draw_overlay_quad(batch, tooltip.panel.center, tooltip.panel.half_size, hud_panel_color, tooltip_alpha);
    draw_hud_panel(batch, tooltip.panel);
    draw_text_layout(batch, text.font, tooltip.name, find_text_style(text, style_heading).color);
    draw_text_layout(batch, text.font, tooltip.description, find_text_style(text, style_tile_label_small).color);
    for (const text_layout& change : tooltip.changes) {
        draw_text_layout(batch, text.font, change, find_text_style(text, style_card_detail).color);
    }
}
