#include "renderer/control_icons.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

void draw_button_outline(overlay_batch& batch,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const glm::vec3 color,
                         const float alpha) {
    constexpr float line_thickness = 0.008f;
    const glm::vec2 top_left(center.x - half_size.x, center.y + half_size.y);
    const glm::vec2 top_right(center.x + half_size.x, center.y + half_size.y);
    const glm::vec2 bottom_left(center.x - half_size.x, center.y - half_size.y);
    const glm::vec2 bottom_right(center.x + half_size.x, center.y - half_size.y);

    draw_overlay_segment(batch, top_left, top_right, line_thickness, color, alpha);
    draw_overlay_segment(batch, top_right, bottom_right, line_thickness, color, alpha);
    draw_overlay_segment(batch, bottom_right, bottom_left, line_thickness, color, alpha);
    draw_overlay_segment(batch, bottom_left, top_left, line_thickness, color, alpha);
}

namespace {
const glm::vec3 icon_color(0.70f, 0.72f, 0.70f);
constexpr float icon_alpha = 0.58f;
}

void draw_key_button(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size) {
    draw_overlay_quad(batch, center, half_size, glm::vec3(0.12f, 0.13f, 0.13f), 0.08f);
    draw_button_outline(batch, center, half_size, glm::vec3(0.66f, 0.68f, 0.66f), 0.58f);
}

void draw_arrow_icon(overlay_batch& batch,
                     const glm::vec2 center,
                     const glm::vec2 direction,
                     const glm::vec2 half_size) {
    const glm::vec2 dir = glm::normalize(direction);
    const glm::vec2 side(-dir.y, dir.x);
    const float radius = std::min(half_size.x, half_size.y) * 0.66f;
    const float thickness = std::max(0.008f, radius * 0.14f);
    const glm::vec2 tip = center + dir * radius;
    const glm::vec2 tail = center - dir * (radius * 0.48f);
    const glm::vec2 shoulder = tip - dir * (radius * 0.46f);

    draw_overlay_segment(batch, tail, tip, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, tip, shoulder + side * (radius * 0.34f), thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, tip, shoulder - side * (radius * 0.34f), thickness, icon_color, icon_alpha);
}

void draw_space_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size) {
    const float thickness = 0.010f;
    const float width = half_size.x * 1.08f;
    const float height = half_size.y * 0.34f;
    const glm::vec2 left(center.x - width * 0.5f, center.y - height * 0.15f);
    const glm::vec2 right(center.x + width * 0.5f, center.y - height * 0.15f);

    draw_overlay_segment(batch, left, right, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, left, left + glm::vec2(0.0f, height), thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, right, right + glm::vec2(0.0f, height), thickness, icon_color, icon_alpha);
}

void draw_shift_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size) {
    const float thickness = 0.010f;
    const float width = half_size.x * 0.84f;
    const float height = half_size.y * 0.88f;
    const glm::vec2 tip = center + glm::vec2(0.0f, height * 0.48f);
    const glm::vec2 left_shoulder = center + glm::vec2(-width * 0.38f, height * 0.04f);
    const glm::vec2 right_shoulder = center + glm::vec2(width * 0.38f, height * 0.04f);
    const glm::vec2 left_base = center + glm::vec2(-width * 0.20f, -height * 0.48f);
    const glm::vec2 right_base = center + glm::vec2(width * 0.20f, -height * 0.48f);

    draw_overlay_segment(batch, tip, left_shoulder, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, tip, right_shoulder, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, left_shoulder, left_base, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, right_shoulder, right_base, thickness, icon_color, icon_alpha);
    draw_overlay_segment(batch, left_base, right_base, thickness, icon_color, icon_alpha);
}
