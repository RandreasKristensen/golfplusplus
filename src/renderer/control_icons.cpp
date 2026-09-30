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

void draw_control_button_base(overlay_batch& batch,
                              const glm::vec2 center,
                              const glm::vec2 half_size,
                              const bool is_down) {
    const glm::vec3 outline_color(0.66f, 0.68f, 0.66f);
    const glm::vec3 pressed_color(0.88f, 0.70f, 0.30f);

    if (is_down) {
        draw_overlay_quad(batch, center, half_size, pressed_color, 0.90f);
    } else {
        draw_overlay_quad(batch, center, half_size, glm::vec3(0.12f, 0.13f, 0.13f), 0.08f);
    }

    draw_button_outline(batch, center, half_size, outline_color, is_down ? 0.95f : 0.58f);
}

glm::vec3 control_icon_color(const bool is_down) {
    return is_down ? glm::vec3(0.08f, 0.085f, 0.08f) : glm::vec3(0.70f, 0.72f, 0.70f);
}

float control_icon_alpha(const bool is_down) {
    return is_down ? 1.0f : 0.58f;
}

void draw_arrow_icon(overlay_batch& batch,
                     const glm::vec2 center,
                     const glm::vec2 direction,
                     const glm::vec2 half_size,
                     const bool is_down) {
    const glm::vec2 dir = glm::normalize(direction);
    const glm::vec2 side(-dir.y, dir.x);
    const float radius = std::min(half_size.x, half_size.y) * 0.66f;
    const float thickness = std::max(0.008f, radius * 0.14f);
    const glm::vec2 tip = center + dir * radius;
    const glm::vec2 tail = center - dir * (radius * 0.48f);
    const glm::vec2 shoulder = tip - dir * (radius * 0.46f);
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);

    draw_overlay_segment(batch, tail, tip, thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder + side * (radius * 0.34f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder - side * (radius * 0.34f), thickness, color, alpha);
}

void draw_space_icon(overlay_batch& batch,
                     const glm::vec2 center,
                     const glm::vec2 half_size,
                     const glm::vec3 color,
                     const float alpha) {
    const float thickness = 0.010f;
    const float width = half_size.x * 1.08f;
    const float height = half_size.y * 0.34f;
    const glm::vec2 left(center.x - width * 0.5f, center.y - height * 0.15f);
    const glm::vec2 right(center.x + width * 0.5f, center.y - height * 0.15f);

    draw_overlay_segment(batch, left, right, thickness, color, alpha);
    draw_overlay_segment(batch, left, left + glm::vec2(0.0f, height), thickness, color, alpha);
    draw_overlay_segment(batch, right, right + glm::vec2(0.0f, height), thickness, color, alpha);
}

void draw_space_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    draw_space_icon(batch, center, half_size, control_icon_color(is_down), control_icon_alpha(is_down));
}

void draw_shift_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const float width = half_size.x * 0.84f;
    const float height = half_size.y * 0.88f;
    const glm::vec2 tip = center + glm::vec2(0.0f, height * 0.48f);
    const glm::vec2 left_shoulder = center + glm::vec2(-width * 0.38f, height * 0.04f);
    const glm::vec2 right_shoulder = center + glm::vec2(width * 0.38f, height * 0.04f);
    const glm::vec2 left_base = center + glm::vec2(-width * 0.20f, -height * 0.48f);
    const glm::vec2 right_base = center + glm::vec2(width * 0.20f, -height * 0.48f);

    draw_overlay_segment(batch, tip, left_shoulder, thickness, color, alpha);
    draw_overlay_segment(batch, tip, right_shoulder, thickness, color, alpha);
    draw_overlay_segment(batch, left_shoulder, left_base, thickness, color, alpha);
    draw_overlay_segment(batch, right_shoulder, right_base, thickness, color, alpha);
    draw_overlay_segment(batch, left_base, right_base, thickness, color, alpha);
}

void draw_enter_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 top = center + glm::vec2(half_size.x * 0.44f, half_size.y * 0.40f);
    const glm::vec2 turn = center + glm::vec2(half_size.x * 0.44f, -half_size.y * 0.10f);
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.42f, -half_size.y * 0.10f);
    const glm::vec2 shoulder = tip + glm::vec2(half_size.x * 0.30f, 0.0f);

    draw_overlay_segment(batch, top, turn, thickness, color, alpha);
    draw_overlay_segment(batch, turn, tip, thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder + glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, shoulder - glm::vec2(0.0f, half_size.y * 0.22f), thickness, color, alpha);
}

void draw_backspace_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float thickness = 0.010f;
    const glm::vec2 tip = center + glm::vec2(-half_size.x * 0.44f, 0.0f);
    const glm::vec2 mid = center + glm::vec2(half_size.x * 0.20f, 0.0f);

    draw_overlay_segment(batch, tip, mid, thickness, color, alpha);
    draw_overlay_segment(batch, tip, center + glm::vec2(-half_size.x * 0.10f, half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, center + glm::vec2(-half_size.x * 0.10f, -half_size.y * 0.28f), thickness, color, alpha);
    draw_overlay_segment(batch,
                         center + glm::vec2(half_size.x * 0.36f, half_size.y * 0.34f),
                         center + glm::vec2(half_size.x * 0.36f, -half_size.y * 0.34f),
                         thickness,
                         color,
                         alpha);
}

void draw_retee_icon(overlay_batch& batch, const glm::vec2 center, const glm::vec2 half_size, const bool is_down) {
    const glm::vec3 color = control_icon_color(is_down);
    const float alpha = control_icon_alpha(is_down);
    const float radius = std::min(half_size.x, half_size.y) * 0.52f;
    const float thickness = 0.010f;
    constexpr int segment_count = 12;
    constexpr float start_angle = -0.45f;
    constexpr float end_angle = 4.65f;

    glm::vec2 previous = center + glm::vec2(std::cos(start_angle), std::sin(start_angle)) * radius;
    for (int i = 1; i <= segment_count; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(segment_count);
        const float angle = start_angle + (end_angle - start_angle) * t;
        const glm::vec2 next = center + glm::vec2(std::cos(angle), std::sin(angle)) * radius;
        draw_overlay_segment(batch, previous, next, thickness, color, alpha);
        previous = next;
    }

    const glm::vec2 tip = center + glm::vec2(std::cos(end_angle), std::sin(end_angle)) * radius;
    draw_overlay_segment(batch, tip, tip + glm::vec2(half_size.x * 0.18f, half_size.y * 0.08f), thickness, color, alpha);
    draw_overlay_segment(batch, tip, tip + glm::vec2(half_size.x * 0.04f, -half_size.y * 0.22f), thickness, color, alpha);
}
