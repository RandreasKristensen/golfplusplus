#include "renderer/rangefinder_overlay.h"

#include "game/text_ids.h"
#include "renderer/hud_overlay.h"
#include "renderer/hud_panel.h"
#include "renderer/pixel_font.h"
#include "renderer/ui_rect.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace {
const glm::vec3 lcd_shadow(0.02f, 0.02f, 0.02f);
const glm::vec3 wind_color(0.88f, 0.86f, 0.72f);

// The round field of view: radius in clip y, and clip x per clip y so it is
// round on the screen at any aspect ratio.
struct eyepiece {
    float radius;
    float x_per_y;

    glm::vec2 point(const float angle, const float radius_share) const {
        return glm::vec2(std::cos(angle) * x_per_y, std::sin(angle)) * (radius * radius_share);
    }
    // A box at `center` with `half` its size, both in clip y, so it keeps its shape.
    ui_rect box(const glm::vec2 center, const glm::vec2 half) const {
        return ui_rect{glm::vec2(center.x * x_per_y, center.y), glm::vec2(half.x * x_per_y, half.y)};
    }
};

// Clip units per pixel of the overlay's target, so lines stay one pixel thin.
glm::vec2 pixel_size(const overlay_grid& grid) {
    return glm::vec2(grid.width > 0 ? 2.0f / static_cast<float>(grid.width) : 0.004f,
                     grid.height > 0 ? 2.0f / static_cast<float>(grid.height) : 0.006f);
}

// A box `half_pixels` in size, `offset_pixels` from `center`.
void draw_pixel_box(overlay_batch& batch, const glm::vec2 center, const glm::vec2 offset_pixels, const glm::vec2 half_pixels,
                    const glm::vec3 color, const float alpha) {
    const glm::vec2 pixel = pixel_size(batch.grid);
    draw_overlay_quad(batch, center + offset_pixels * pixel, half_pixels * pixel, color, alpha);
}

// A ring round the centre of the screen between two radii in pixels.
void draw_pixel_ring(overlay_batch& batch, const float inner, const float outer, const glm::vec3 color, const float alpha) {
    constexpr int segments = 24;
    const glm::vec2 pixel = pixel_size(batch.grid);
    const auto at = [&](const float angle, const float radius) {
        return glm::vec2(std::cos(angle), std::sin(angle)) * radius * pixel;
    };
    for (int i = 0; i < segments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float b = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        draw_overlay_corner_quad(batch, at(a, inner), at(a, outer), at(b, outer), at(b, inner), color, alpha);
    }
}

// The eyepiece's solid outline just outside the magnified view, with a
// faint inner edge.
void draw_eyepiece_outline(overlay_batch& batch, const eyepiece& view) {
    constexpr int segments = 96;
    const float pixel = pixel_size(batch.grid).y / view.radius;
    const float inner = 1.0f - 0.5f * pixel;
    const float outer = 1.0f + static_cast<float>(eyepiece_outline_pixels) * pixel;
    for (int i = 0; i < segments; ++i) {
        const float a = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(segments);
        const float b = glm::two_pi<float>() * static_cast<float>(i + 1) / static_cast<float>(segments);
        draw_overlay_corner_quad(batch, view.point(a, inner), view.point(a, outer), view.point(b, outer), view.point(b, inner),
                                 hud_panel_color, 1.0f);
        draw_overlay_corner_quad(batch, view.point(a, inner), view.point(a, inner + pixel), view.point(b, inner + pixel),
                                 view.point(b, inner), hud_outline_color, 0.54f);
    }
}

// The aiming circle with crosshairs outside it and, locked onto the flag,
// the lock ring round it; each on a dark edge so it reads on a bright sky.
void draw_reticle(overlay_batch& batch, const bool lock_lit) {
    constexpr float circle = 5.0f;
    constexpr float lock_ring = 8.0f;
    constexpr float tick_from = 11.0f;
    constexpr float tick_length = 5.0f;
    const float middle = tick_from + tick_length * 0.5f;
    for (const bool shadow : {true, false}) {
        const glm::vec3 color = shadow ? lcd_shadow : hud_amber;
        const float alpha = shadow ? 0.45f : 0.92f;
        const float grow = shadow ? 1.0f : 0.0f;
        draw_pixel_ring(batch, circle - 0.5f - grow, circle + 0.5f + grow, color, alpha);
        if (lock_lit) {
            draw_pixel_ring(batch, lock_ring - 0.5f - grow, lock_ring + 0.5f + grow, color, alpha);
        }
        const glm::vec2 long_half(tick_length * 0.5f + grow, 0.5f + grow);
        const glm::vec2 tall_half(long_half.y, long_half.x);
        draw_pixel_box(batch, glm::vec2(0.0f), glm::vec2(-middle, 0.0f), long_half, color, alpha);
        draw_pixel_box(batch, glm::vec2(0.0f), glm::vec2(middle, 0.0f), long_half, color, alpha);
        draw_pixel_box(batch, glm::vec2(0.0f), glm::vec2(0.0f, -middle), tall_half, color, alpha);
        draw_pixel_box(batch, glm::vec2(0.0f), glm::vec2(0.0f, middle), tall_half, color, alpha);
    }
}

// Corner brackets round the flagstick from its foot to its top, as the
// eyepiece magnifies it; none outside the eyepiece.
void draw_flag_brackets(overlay_batch& batch, const render_data& data, const glm::mat4& view_proj, const eyepiece& view) {
    const std::optional<glm::vec2> foot = project_to_screen(view_proj, *data.rangefinder_flag);
    const std::optional<glm::vec2> top =
        project_to_screen(view_proj, *data.rangefinder_flag + glm::vec3(0.0f, data.pin_visual_height_meters, 0.0f));
    if (!foot || !top) {
        return;
    }
    const float magnification = data.rangefinder_eyepiece.magnification;
    const glm::vec2 pixel = pixel_size(batch.grid);
    const glm::vec2 center = (*foot + *top) * 0.5f * magnification;
    if (glm::length(glm::vec2(center.x / view.x_per_y, center.y)) > view.radius) {
        return;
    }
    const float half_height = std::max(5.0f, std::abs(top->y - foot->y) * magnification / pixel.y * 0.5f + 3.0f);
    const glm::vec2 half(std::max(4.0f, half_height * 0.45f), half_height);
    const float arm = std::min(half.x, half.y) * 0.6f;
    for (const float sx : {-1.0f, 1.0f}) {
        for (const float sy : {-1.0f, 1.0f}) {
            const glm::vec2 corner(sx * half.x, sy * half.y);
            draw_pixel_box(batch, center, corner - glm::vec2(sx * arm * 0.5f, 0.0f), glm::vec2(arm * 0.5f, 0.5f), hud_amber, 0.92f);
            draw_pixel_box(batch, center, corner - glm::vec2(0.0f, sy * arm * 0.5f), glm::vec2(0.5f, arm * 0.5f), hud_amber, 0.92f);
        }
    }
}

// LCD text in its box, on a dark edge one pixel down and right.
text_layout draw_lcd_label(overlay_batch& batch, const text_assets& text, const text_style& style, const std::string& label,
                           const ui_rect& box) {
    const glm::vec2 pixel = pixel_size(batch.grid);
    const ui_rect shadow_box{box.center + glm::vec2(pixel.x, -pixel.y), box.half_size};
    draw_text_layout(batch, text.font, layout_text(text.font, style, label, shadow_box, batch.grid), lcd_shadow);
    const text_layout layout = layout_text(text.font, style, label, box, batch.grid);
    draw_text_layout(batch, text.font, layout, style.color);
    return layout;
}

// An arrow the way the wind blows, up straight ahead, turned `angle` to the
// left; a dot in still air.
void draw_wind_arrow(overlay_batch& batch, const ui_rect& cell, const std::optional<float> angle) {
    const glm::vec2 pixel = pixel_size(batch.grid);
    const float radius = std::max(3.0f, std::floor(std::min(cell.half_size.x / pixel.x, cell.half_size.y / pixel.y)) - 1.0f);
    if (!angle) {
        draw_pixel_box(batch, cell.center, glm::vec2(0.0f), glm::vec2(1.0f), wind_color, 0.90f);
        return;
    }
    const glm::vec2 along(-std::sin(*angle), std::cos(*angle));
    const glm::vec2 across(along.y, -along.x);
    const auto at = [&](const glm::vec2 pixels) { return cell.center + pixels * pixel; };
    const float head = radius * 0.55f;
    const glm::vec2 tip = along * radius;
    const glm::vec2 neck = along * (radius - head);
    const glm::vec2 tail = -along * radius;
    constexpr float shaft = 0.6f;
    for (const bool shadow : {true, false}) {
        const glm::vec2 shift = shadow ? glm::vec2(1.0f, -1.0f) : glm::vec2(0.0f);
        const glm::vec3 color = shadow ? lcd_shadow : wind_color;
        const float alpha = shadow ? 0.55f : 0.90f;
        draw_overlay_corner_quad(batch, at(shift + tail + across * shaft), at(shift + neck + across * shaft),
                                 at(shift + neck - across * shaft), at(shift + tail - across * shaft), color, alpha);
        draw_overlay_corner_quad(batch, at(shift + tip), at(shift + neck + across * head * 0.8f), at(shift + neck - across * head * 0.8f),
                                 at(shift + neck - across * head * 0.8f), color, alpha);
    }
}
}

void draw_rangefinder(overlay_batch& batch, const text_assets& text, const render_data& data, const glm::mat4& view_proj) {
    const float x_per_y = batch.grid.width > 0 ? static_cast<float>(batch.grid.height) / static_cast<float>(batch.grid.width) : 1.0f;
    const eyepiece view{eyepiece_radius_share, x_per_y};
    draw_eyepiece_outline(batch, view);
    const bool on_flag = data.rangefinder_flag.has_value();
    if (on_flag) {
        draw_flag_brackets(batch, data, view_proj, view);
    }
    draw_reticle(batch, data.rangefinder_lock_lit);

    // The flag lock over the reticle, lit while locked on.
    const text_style& tag = find_text_style(text, style_rangefinder_tag);
    draw_lcd_label(batch, text, on_flag ? tag : with_color(tag, tag.color * 0.30f), lookup_text(text, text_hud_rangefinder_flag),
                   view.box(glm::vec2(0.0f, 0.42f), glm::vec2(0.16f, 0.05f)));

    // The distance under it, its unit small after the digits.
    const ui_rect distance = view.box(glm::vec2(0.0f, -0.48f), glm::vec2(0.24f, 0.08f));
    const text_layout digits = draw_lcd_label(batch, text, find_text_style(text, style_rangefinder), data.rangefinder_distance_label,
                                              slice_x(distance, 0.0f, 0.62f));
    const ui_rect unit_column = slice_x(distance, 0.64f, 1.0f);
    draw_lcd_label(batch, text, find_text_style(text, style_rangefinder_unit), lookup_text(text, text_hud_rangefinder_unit),
                   rect_from_edges(rect_left(unit_column), rect_bottom(digits.bounds), rect_right(unit_column), rect_top(digits.bounds)));

    // The height over my ground under it.
    draw_lcd_label(batch, text, find_text_style(text, style_rangefinder_height), data.rangefinder_height_label,
                   view.box(glm::vec2(0.0f, -0.63f), glm::vec2(0.14f, 0.05f)));

    // The wind, lower left in the field like a status icon.
    draw_wind_arrow(batch, view.box(glm::vec2(-0.62f, -0.32f), glm::vec2(0.05f, 0.05f)), data.rangefinder_wind_angle);
    draw_lcd_label(batch, text, find_text_style(text, style_rangefinder_wind), data.rangefinder_wind_label,
                   view.box(glm::vec2(-0.35f, -0.32f), glm::vec2(0.20f, 0.05f)));
}
