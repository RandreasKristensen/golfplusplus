#include "renderer/course_map_card.h"

#include "game/text_ids.h"
#include "renderer/pixel_font.h"

#include <array>
#include <cmath>
#include <string>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// The card's sizes in screen pixels: the chips' distance from the paper's
// edge and their padding, the north arrow's chip, arrow and letter, and the scale bar's rows.
constexpr float card_pad = 6.0f;
constexpr float chip_pad = 4.0f;
constexpr float compass_half = 15.0f;
constexpr float arrow_tip = 6.0f;
constexpr float arrow_tail = 5.0f;
constexpr float arrow_half_width = 3.5f;
constexpr float arrow_outline = 1.0f;
constexpr float north_letter_distance = 10.5f;
constexpr float north_letter_half = 4.5f;
constexpr float scale_label_height = 9.0f;
constexpr float scale_bar_height = 7.0f;
constexpr float scale_bar_thickness = 2.0f;
constexpr float scale_tick_width = 1.0f;
// The scale bar is at most this share of the paper's width.
constexpr float scale_widest_share = 0.25f;
constexpr float chip_alpha = 0.88f;
constexpr float chip_outline_alpha = 0.45f;
constexpr std::array<int, 3> scale_steps{1, 2, 5};

float scale_bar_pixels(const course_map_layout& layout, const overlay_grid grid, const float meters_per_unit, const int meters) {
    return static_cast<float>(meters) * layout.scale.y * pixels_per_clip(grid).y / meters_per_unit;
}

// A chip of `size` pixels in the paper's corner, inside the card's padding.
ui_rect corner_chip(const course_map_layout& layout, const overlay_grid grid, const bool right, const bool top, const glm::vec2 size) {
    const glm::vec2 per_clip = pixels_per_clip(grid);
    const glm::vec2 half = size * 0.5f / per_clip;
    const glm::vec2 corner(right ? 1.0f : -1.0f, top ? 1.0f : -1.0f);
    return ui_rect{layout.center + corner * (layout.half_size - glm::vec2(card_pad) / per_clip - half), half};
}

ui_rect compass_chip(const course_map_layout& layout, const overlay_grid grid) {
    return corner_chip(layout, grid, true, true, glm::vec2(compass_half * 2.0f));
}

ui_rect scale_chip(const course_map_layout& layout, const overlay_grid grid, const float meters_per_unit) {
    const float bar = scale_bar_pixels(layout, grid, meters_per_unit, course_map_scale_meters(layout, grid, meters_per_unit));
    return corner_chip(layout, grid, false, false,
                       glm::vec2(bar + chip_pad * 2.0f, scale_label_height + scale_bar_height + chip_pad));
}

void draw_chip(overlay_batch& batch, const ui_rect& chip, const glm::vec3 paper, const glm::vec3 outline) {
    const glm::vec2 one = glm::vec2(1.0f) / pixels_per_clip(batch.grid);
    draw_overlay_quad(batch, chip.center, chip.half_size + one, outline, chip_outline_alpha);
    draw_overlay_quad(batch, chip.center, chip.half_size, paper, chip_alpha);
}

void draw_north_arrow(overlay_batch& batch, const text_assets& text, const course_map_layout& layout, const map_inks& inks) {
    const glm::vec2 per_clip = pixels_per_clip(batch.grid);
    const ui_rect chip = compass_chip(layout, batch.grid);
    draw_chip(batch, chip, inks.paper, inks.marker_outline);
    const glm::vec2 north = course_map_north(layout);
    const glm::vec2 side(-north.y, north.x);
    const auto point = [&](const float along, const float across) {
        return chip.center + (north * along + side * across) / per_clip;
    };
    const auto triangle = [&](const float grow, const glm::vec3 ink) {
        const glm::vec2 tip = point(arrow_tip + grow, 0.0f);
        draw_overlay_corner_quad(batch, point(-arrow_tail - grow, arrow_half_width + grow), tip, tip,
                                 point(-arrow_tail - grow, -arrow_half_width - grow), ink, 1.0f);
    };
    triangle(arrow_outline, inks.marker_outline);
    triangle(0.0f, inks.flag);
    const glm::vec2 letter = point(north_letter_distance, 0.0f);
    draw_label(batch, text.font, find_text_style(text, style_course_map_scale), lookup_text(text, text_course_map_north),
               ui_rect{letter, glm::vec2(north_letter_half) / per_clip});
}

void draw_scale_bar(overlay_batch& batch, const text_assets& text, const course_map_layout& layout, const render_data& data,
                    const map_inks& inks) {
    const glm::vec2 per_clip = pixels_per_clip(batch.grid);
    const int meters = course_map_scale_meters(layout, batch.grid, data.map_meters_per_unit);
    const ui_rect chip = scale_chip(layout, batch.grid, data.map_meters_per_unit);
    draw_chip(batch, chip, inks.paper, inks.marker_outline);
    const glm::vec2 inner = chip.half_size - glm::vec2(chip_pad * 0.5f) / per_clip;
    const ui_rect label{chip.center + glm::vec2(0.0f, inner.y - scale_label_height * 0.5f / per_clip.y),
                        glm::vec2(inner.x, scale_label_height * 0.5f / per_clip.y)};
    draw_label(batch, text.font, find_text_style(text, style_course_map_scale),
               format_text(text, text_course_map_scale, {{"meters", std::to_string(meters)}}), label);
    // The bar along the chip's bottom, a tick at each end.
    const float bar = chip.half_size.x * 2.0f * per_clip.x - chip_pad * 2.0f;
    const glm::vec2 middle = glm::vec2(chip.center.x, rect_bottom(chip) + (chip_pad * 0.5f + scale_bar_height * 0.5f) / per_clip.y);
    draw_overlay_quad(batch, middle, glm::vec2(bar * 0.5f, scale_bar_thickness * 0.5f) / per_clip, inks.marker_outline);
    for (const float end : {-1.0f, 1.0f}) {
        draw_overlay_quad(batch, middle + glm::vec2(end * (bar * 0.5f - scale_tick_width * 0.5f), 0.0f) / per_clip,
                          glm::vec2(scale_tick_width * 0.5f, scale_bar_height * 0.5f) / per_clip, inks.marker_outline);
    }
}
}

int course_map_scale_meters(const course_map_layout& layout, const overlay_grid grid, const float meters_per_unit) {
    const float widest = layout.half_size.x * 2.0f * pixels_per_clip(grid).x * scale_widest_share;
    int best = 1;
    for (int decade = 1; decade <= 100000; decade *= 10) {
        for (const int step : scale_steps) {
            if (scale_bar_pixels(layout, grid, meters_per_unit, step * decade) <= widest) {
                best = step * decade;
            }
        }
    }
    return best;
}

std::vector<ui_rect> course_map_card_boxes(const course_map_layout& layout, const overlay_grid grid, const render_data& data) {
    return {compass_chip(layout, grid), scale_chip(layout, grid, data.map_meters_per_unit)};
}

void draw_course_map_card(overlay_batch& batch,
                          const text_assets& text,
                          const course_map_layout& layout,
                          const render_data& data,
                          const map_inks& inks) {
    draw_north_arrow(batch, text, layout, inks);
    draw_scale_bar(batch, text, layout, data, inks);
}
