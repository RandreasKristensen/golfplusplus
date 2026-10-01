#include "renderer/hud_overlay.h"

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec4.hpp>

namespace {
void draw_control_key(overlay_batch& batch,
                      const text_assets& text,
                      const char* key,
                      const glm::vec2 center,
                      const glm::vec2 half_size,
                      const bool is_down) {
    draw_control_button_base(batch, center, half_size, is_down);
    draw_text_fitted(batch,
                     text.font,
                     find_text_style(text, is_down ? style_control_key_down : style_control_key),
                     lookup_text(text, key),
                     center,
                     half_size);
}

void draw_controls_overlay(overlay_batch& batch, const text_assets& text, const controls_overlay_state& controls) {
    const glm::vec2 dpad_half(0.060f, 0.060f);
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 dpad_center(0.79f, 0.02f);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, 0.126f), dpad_half, controls.up_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, 0.126f), glm::vec2(0.0f, 1.0f), dpad_half, controls.up_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(-0.070f, 0.0f), dpad_half, controls.left_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(-0.070f, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, controls.left_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.070f, 0.0f), dpad_half, controls.right_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.070f, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, controls.right_down);

    draw_control_button_base(batch, dpad_center + glm::vec2(0.0f, -0.126f), dpad_half, controls.down_down);
    draw_arrow_icon(batch, dpad_center + glm::vec2(0.0f, -0.126f), glm::vec2(0.0f, -1.0f), dpad_half, controls.down_down);

    draw_control_button_base(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);
    draw_space_icon(batch, glm::vec2(0.79f, -0.32f), wide_half, controls.space_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);
    draw_shift_icon(batch, glm::vec2(0.70f, -0.46f), small_half, controls.shift_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);
    draw_enter_icon(batch, glm::vec2(0.88f, -0.46f), small_half, controls.enter_down);

    draw_control_button_base(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);
    draw_backspace_icon(batch, glm::vec2(0.70f, -0.60f), small_half, controls.backspace_down);

    draw_control_button_base(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);
    draw_retee_icon(batch, glm::vec2(0.88f, -0.60f), small_half, controls.retee_down);

    const glm::vec2 key_half(0.052f, 0.046f);
    draw_control_key(batch, text, text_controls_key_1, glm::vec2(0.70f, -0.74f), key_half, controls.key_1_down);
    draw_control_key(batch, text, text_controls_key_2, glm::vec2(0.88f, -0.74f), key_half, controls.key_2_down);
}


void draw_fps_counter(overlay_batch& batch, const text_assets& text, const std::string& label) {
    if (label.empty()) {
        return;
    }

    draw_text(batch, text.font, find_text_style(text, style_debug_fps), label, glm::vec2(-0.96f, 0.92f));
}

// Developer diagnostics: the lines come from profiling, not the string table.
void draw_profile_overlay(overlay_batch& batch, const text_assets& text, const frame_profile& profile) {
    const std::vector<std::string> lines = format_profile_overlay_lines(profile);
    if (lines.empty()) {
        return;
    }

    constexpr float line_step = 0.052f;
    const text_style& style = find_text_style(text, style_debug_profile);
    glm::vec2 cursor(-0.96f, 0.855f);
    for (const std::string& line : lines) {
        draw_text(batch, text.font, style, line, cursor);
        cursor.y -= line_step;
    }
}

void draw_club_label(overlay_batch& batch, const text_assets& text, const std::string& label) {
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    const glm::vec2 panel_center(0.78f, 0.78f);
    const glm::vec2 panel_half(0.17f, 0.12f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color);

    draw_text_fitted(batch, text.font, find_text_style(text, style_hud_club), label, panel_center, panel_half);
}

void draw_interact_prompt(overlay_batch& batch) {
    const glm::vec3 prompt_color(0.95f, 0.82f, 0.28f);
    draw_space_icon(batch, glm::vec2(0.0f, -0.56f), glm::vec2(0.16f, 0.075f), prompt_color, 1.0f);
}

void draw_power_meter(overlay_batch& batch, const text_assets& text, const float swing_power) {
    const float power = std::clamp(swing_power, 0.0f, 1.0f);
    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const glm::vec3 amber(0.92f, 0.70f, 0.18f);

    const glm::vec2 panel_center(-0.62f, -0.72f);
    const glm::vec2 panel_half(0.32f, 0.155f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.014f), panel_half, glm::vec3(0.0f), 0.22f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.78f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.54f);

    draw_text(batch, text.font, find_text_style(text, style_hud_label), lookup_text(text, text_hud_power), panel_center + glm::vec2(-0.275f, 0.105f));

    const glm::vec2 track_center = panel_center + glm::vec2(0.020f, -0.012f);
    const glm::vec2 track_half(0.245f, 0.035f);
    const float track_left = track_center.x - track_half.x;
    const float track_right = track_center.x + track_half.x;
    const float track_width = track_half.x * 2.0f;

    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.030f, 0.032f, 0.034f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.56f);

    if (power > 0.0f) {
        const float fill_half_width = track_half.x * power;
        const glm::vec2 fill_center(track_left + fill_half_width, track_center.y);
        draw_overlay_quad(batch, fill_center, glm::vec2(fill_half_width, track_half.y * 0.58f), amber, 0.92f);
    }

    const float needle_x = track_left + track_width * power;
    draw_overlay_segment(batch,
                         glm::vec2(needle_x, track_center.y - 0.055f),
                         glm::vec2(needle_x, track_center.y + 0.055f),
                         0.008f,
                         glm::vec3(0.98f, 0.80f, 0.24f),
                         0.96f);

    const std::array<float, 3> ticks{0.0f, 0.5f, 1.0f};
    for (const float tick : ticks) {
        const float x = track_left + track_width * tick;
        draw_overlay_segment(batch,
                             glm::vec2(x, track_center.y - 0.058f),
                             glm::vec2(x, track_center.y - 0.080f),
                             0.005f,
                             outline_color,
                             0.72f);
    }

    const text_style& scale_style = find_text_style(text, style_hud_scale);
    const float scale_y = panel_center.y - 0.112f;
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_min), glm::vec2(track_left, scale_y));
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_mid), glm::vec2(track_center.x, scale_y));
    draw_text(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_max), glm::vec2(track_right, scale_y));
}

void draw_cart_hud(overlay_batch& batch, const text_assets& text, const render_data& data) {
    if (!data.cart_active) {
        return;
    }

    const glm::vec3 panel_color(0.055f, 0.060f, 0.065f);
    const glm::vec3 outline_color(0.60f, 0.62f, 0.56f);
    const glm::vec3 meter_color = data.cart_drifting ? glm::vec3(0.96f, 0.56f, 0.22f) : glm::vec3(0.72f, 0.80f, 0.36f);

    const glm::vec2 panel_center(-0.74f, 0.72f);
    const glm::vec2 panel_half(0.19f, 0.13f);
    draw_overlay_quad(batch, panel_center + glm::vec2(0.012f, -0.012f), panel_half, glm::vec3(0.0f), 0.18f);
    draw_overlay_quad(batch, panel_center, panel_half, panel_color, 0.82f);
    draw_button_outline(batch, panel_center, panel_half, outline_color, 0.52f);

    draw_text(batch, text.font, find_text_style(text, style_cart_label), lookup_text(text, text_hud_cart), panel_center + glm::vec2(-0.150f, 0.082f));
    draw_text(batch,
              text.font,
              find_text_style(text, data.cart_drifting ? style_cart_drift : style_cart_drive),
              lookup_text(text, data.cart_drifting ? text_hud_cart_drift : text_hud_cart_drive),
              panel_center + glm::vec2(-0.150f, 0.020f));

    const glm::vec2 track_center = panel_center + glm::vec2(0.030f, -0.046f);
    const glm::vec2 track_half(0.120f, 0.024f);
    draw_overlay_quad(batch, track_center, track_half, glm::vec3(0.028f, 0.030f, 0.032f), 0.92f);
    draw_button_outline(batch, track_center, track_half, outline_color, 0.46f);

    const float speed_ratio = std::clamp(data.cart_speed_fraction, 0.0f, 1.0f);
    if (speed_ratio > 0.0f) {
        const float fill_half_width = track_half.x * speed_ratio;
        draw_overlay_quad(batch,
                          glm::vec2(track_center.x - track_half.x + fill_half_width, track_center.y),
                          glm::vec2(fill_half_width, track_half.y * 0.60f),
                          meter_color,
                          0.94f);
    }
}

void draw_skills_panel(overlay_batch& batch, const text_assets& text, const std::vector<render_skill_progress>& skills) {
    if (skills.empty()) {
        return;
    }

    const glm::vec2 center(0.0f, 0.16f);
    const glm::vec2 half(0.58f, 0.44f);
    const glm::vec3 panel_color(0.050f, 0.055f, 0.060f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const text_style& header = find_text_style(text, style_panel_header);
    const text_style& value = find_text_style(text, style_panel_value);

    draw_overlay_quad(batch, center + glm::vec2(0.018f, -0.020f), half, glm::vec3(0.0f), 0.32f);
    draw_overlay_quad(batch, center, half, panel_color, 0.88f);
    draw_button_outline(batch, center, half, outline_color, 0.64f);

    draw_text(batch, text.font, find_text_style(text, style_panel_title), lookup_text(text, text_skills_title), center + glm::vec2(0.0f, half.y - 0.070f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_name), center + glm::vec2(-0.48f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_level), center + glm::vec2(0.02f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_xp), center + glm::vec2(0.16f, half.y - 0.145f));
    draw_text(batch, text.font, header, lookup_text(text, text_skills_header_next), center + glm::vec2(0.36f, half.y - 0.145f));

    constexpr float row_gap = 0.105f;
    for (std::size_t i = 0; i < skills.size(); ++i) {
        const render_skill_progress& skill = skills[i];
        const float y = center.y + half.y - 0.225f - static_cast<float>(i) * row_gap;
        const glm::vec3 row_color = i % 2 == 0 ? glm::vec3(0.075f, 0.080f, 0.078f) : glm::vec3(0.060f, 0.064f, 0.066f);
        draw_overlay_quad(batch, glm::vec2(center.x, y - 0.016f), glm::vec2(0.50f, 0.038f), row_color, 0.74f);
        draw_text(batch, text.font, value, skill.label, glm::vec2(center.x - 0.48f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(1, skill.level)), glm::vec2(center.x + 0.02f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(0, skill.xp)), glm::vec2(center.x + 0.16f, y));
        draw_text(batch, text.font, value, std::to_string(std::max(0, skill.xp_to_next)), glm::vec2(center.x + 0.36f, y));
    }

    draw_text(batch, text.font, find_text_style(text, style_panel_hint), lookup_text(text, text_skills_hint), center + glm::vec2(0.0f, -half.y + 0.055f));
}

void draw_xp_icon_cell(overlay_batch& batch,
                       const glm::vec2 icon_center,
                       const int x,
                       const int y,
                       const float cell_size,
                       const glm::vec3 color,
                       const float alpha) {
    draw_overlay_quad(batch,
                      icon_center + glm::vec2((static_cast<float>(x) - 2.0f) * cell_size,
                                              (2.0f - static_cast<float>(y)) * cell_size),
                      glm::vec2(cell_size * 0.43f),
                      color,
                      alpha);
}

void draw_xp_icon_pattern(overlay_batch& batch,
                          const glm::vec2 icon_center,
                          const std::array<const char*, 5>& pattern,
                          const float cell_size,
                          const glm::vec3 color,
                          const float alpha) {
    for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
            if (pattern[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)] == '#') {
                draw_xp_icon_cell(batch, icon_center, x, y, cell_size, color, alpha);
            }
        }
    }
}

void draw_xp_icon(overlay_batch& batch,
                  const skill_icon_id icon,
                  const glm::vec2 center,
                  const float alpha) {
    const float cell = 0.010f;
    switch (icon) {
    case skill_icon_id::golf_swing:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", "..#..", ".##..", "...#.", "...##"}},
                             cell,
                             glm::vec3(0.92f, 0.78f, 0.38f),
                             alpha);
        draw_overlay_quad(batch, center + glm::vec2(-0.016f, -0.020f), glm::vec2(0.008f), glm::vec3(0.92f), alpha);
        break;
    case skill_icon_id::smoking:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".###.", "...#.", "####.", "....."}},
                             cell,
                             glm::vec3(0.90f, 0.84f, 0.66f),
                             alpha);
        draw_xp_icon_cell(batch, center, 4, 2, cell, glm::vec3(0.86f, 0.38f, 0.22f), alpha);
        break;
    case skill_icon_id::fitness:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{".....", ".##..", ".###.", "..###", "....."}},
                             cell,
                             glm::vec3(0.52f, 0.82f, 0.58f),
                             alpha);
        break;
    case skill_icon_id::generic:
    default:
        draw_xp_icon_pattern(batch,
                             center,
                             std::array<const char*, 5>{{"..#..", ".###.", "#####", ".###.", "..#.."}},
                             cell,
                             glm::vec3(0.70f, 0.78f, 0.92f),
                             alpha);
        break;
    }
}

void draw_xp_drops(overlay_batch& batch, const text_assets& text, const std::vector<render_xp_drop>& drops) {
    if (drops.empty()) {
        return;
    }

    const glm::vec3 panel_color(0.035f, 0.040f, 0.040f);
    const glm::vec3 outline_color(0.68f, 0.70f, 0.56f);
    const text_style& label_style = find_text_style(text, style_xp_drop);
    const glm::vec2 row_half(0.155f, 0.043f);
    const float row_gap = 0.092f;
    const float right = 0.585f;
    const float start_y = 0.565f;
    const float travel_y = 0.62f;

    for (std::size_t i = 0; i < drops.size(); ++i) {
        const render_xp_drop& drop = drops[i];
        if (drop.xp <= 0) {
            continue;
        }

        const float progress = std::clamp(drop.progress, 0.0f, 1.0f);
        const float alpha = progress > 0.78f ? std::max(0.16f, 1.0f - (progress - 0.78f) / 0.22f) : 1.0f;
        const glm::vec2 center(right - row_half.x, start_y - static_cast<float>(i) * row_gap + progress * travel_y);

        draw_overlay_quad(batch, center + glm::vec2(0.010f, -0.010f), row_half, glm::vec3(0.0f), 0.18f * alpha);
        draw_overlay_quad(batch, center, row_half, panel_color, 0.72f * alpha);
        draw_button_outline(batch, center, row_half, outline_color, 0.42f * alpha);
        draw_xp_icon(batch, drop.icon, center + glm::vec2(-0.105f, 0.0f), alpha);

        const std::string label = format_text(text, text_hud_xp_drop, {{"xp", std::to_string(std::max(0, drop.xp))}});
        draw_text_fitted(batch,
                         text.font,
                         with_color(label_style, label_style.color * alpha),
                         label,
                         center + glm::vec2(-0.055f, 0.021f),
                         glm::vec2(0.100f, 0.030f));
    }
}


// Overlay clip-space position of a world point, or nullopt when it is
// behind the camera or outside the depth range.
std::optional<glm::vec2> project_to_screen(const glm::mat4& view_proj, const glm::vec3& world_position) {
    const glm::vec4 clip = view_proj * glm::vec4(world_position, 1.0f);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.z < -1.0f || ndc.z > 1.0f) {
        return std::nullopt;
    }
    return glm::vec2(ndc.x, ndc.y);
}

void draw_rangefinder_view(overlay_batch& batch,
                           const text_assets& text,
                           const glm::mat4& view_proj,
                           const render_data& data) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.16f, 0.34f, 0.24f), 0.18f);
    draw_overlay_quad(batch, glm::vec2(-0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.91f, 0.0f), glm::vec2(0.18f, 1.0f), glm::vec3(0.01f, 0.018f, 0.014f), 0.52f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.89f), glm::vec2(1.0f, 0.22f), glm::vec3(0.01f, 0.018f, 0.014f), 0.45f);

    const glm::vec3 reticle_color(0.62f, 0.96f, 0.64f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.006f, 0.11f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.11f, 0.006f), reticle_color, 0.72f);
    draw_overlay_quad(batch, glm::vec2(-0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.18f, 0.0f), glm::vec2(0.055f, 0.006f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, -0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.18f), glm::vec2(0.006f, 0.055f), reticle_color, 0.56f);

    const glm::vec3 label_anchor = data.rangefinder_target + glm::vec3(0.0f, data.pin_visual_height_meters + 0.36f, 0.0f);
    const std::optional<glm::vec2> projected = project_to_screen(view_proj, label_anchor);
    if (!projected) {
        return;
    }

    glm::vec2 pin_screen = *projected;
    pin_screen.x = std::max(-0.78f, std::min(0.78f, pin_screen.x));
    pin_screen.y = std::max(-0.68f, std::min(0.82f, pin_screen.y));
    const glm::vec2 label_max_half(0.22f, 0.065f);
    const text_style& style = find_text_style(text, style_rangefinder);
    const float pixel_size = fitted_pixel_size(text.font, style, data.rangefinder_label, label_max_half);
    const float label_width = pixel_text_width(text.font, data.rangefinder_label, pixel_size);
    const glm::vec2 panel_half(std::max(0.12f, label_width * 0.5f + 0.035f), 0.070f);
    draw_overlay_quad(batch, pin_screen + glm::vec2(0.0f, 0.015f), panel_half, glm::vec3(0.015f, 0.032f, 0.024f), 0.78f);
    draw_text(batch,
              text.font,
              with_pixel_size(style, pixel_size),
              data.rangefinder_label,
              pin_screen + glm::vec2(0.0f, 0.015f));
}

std::string score_label(const scorecard_row& row) {
    return row.played ? std::to_string(row.strokes) : "";
}

std::string relative_label(const scorecard_row& row) {
    return row.played ? row.relative_label : "";
}

std::string hole_label(const text_assets& text, const scorecard_row& row, const bool compact) {
    if (compact || row.hole_name.empty()) {
        return std::to_string(row.hole_number);
    }
    return format_text(text, text_scorecard_hole_row, {{"hole", std::to_string(row.hole_number)}, {"name", row.hole_name}});
}

// Scorecard cells are centred in their column; the first column is left
// aligned but vertically centred on the row like the others.
glm::vec2 left_cell_anchor(const pixel_font_data& font, const float x, const float y, const float pixel_size) {
    return glm::vec2(x, y + static_cast<float>(font.height) * 0.5f * pixel_size);
}

void draw_paper_card_base(overlay_batch& batch,
                          const glm::vec2 center,
                          const glm::vec2 half_size,
                          const float alpha) {
    const glm::vec3 paper(0.78f, 0.73f, 0.56f);
    const glm::vec3 paper_shadow(0.12f, 0.095f, 0.065f);
    const glm::vec3 fold(0.42f, 0.35f, 0.24f);

    draw_overlay_quad(batch, center + glm::vec2(0.030f, -0.034f), half_size, paper_shadow, 0.40f * alpha);
    draw_overlay_quad(batch, center, half_size, paper, 0.97f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(-half_size.x * 0.28f, 0.0f), glm::vec2(0.004f, half_size.y), fold, 0.14f * alpha);
    draw_overlay_quad(batch, center + glm::vec2(half_size.x * 0.22f, 0.0f), glm::vec2(0.003f, half_size.y), fold, 0.10f * alpha);
    draw_button_outline(batch, center, half_size, fold, 0.44f * alpha);
}

void draw_scorecard_grid_lines(overlay_batch& batch,
                               const glm::vec2 center,
                               const glm::vec2 half_size,
                               const std::array<float, 5>& x_edges,
                               const float header_y,
                               const float row_height,
                               const int row_count,
                               const float alpha) {
    const glm::vec3 ink(0.23f, 0.19f, 0.13f);
    const float top = header_y + row_height * 0.62f;
    const float bottom = header_y - row_height * (static_cast<float>(row_count) + 0.62f);
    for (float x : x_edges) {
        draw_overlay_segment(batch, glm::vec2(x, top), glm::vec2(x, bottom), 0.004f, ink, 0.38f * alpha);
    }
    for (int i = 0; i <= row_count + 1; ++i) {
        const float y = top - static_cast<float>(i) * row_height;
        draw_overlay_segment(batch, glm::vec2(x_edges.front(), y), glm::vec2(x_edges.back(), y), 0.004f, ink, 0.34f * alpha);
    }
    draw_button_outline(batch, center, half_size, ink, 0.22f * alpha);
}

void draw_scorecard_headers(overlay_batch& batch,
                            const text_assets& text,
                            const std::array<float, 5>& x_edges,
                            const float y,
                            const text_style& style) {
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_hole), glm::vec2((x_edges[0] + x_edges[1]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_score), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, style, lookup_text(text, text_scorecard_header_relative), glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_row_text(overlay_batch& batch,
                             const text_assets& text,
                             const scorecard_row& row,
                             const std::array<float, 5>& x_edges,
                             const float y,
                             const float pixel,
                             const bool compact,
                             const bool current) {
    const text_style ink = with_pixel_size(find_text_style(text, current ? style_scorecard_row_current : style_scorecard_row), pixel);
    const text_style score_ink = row.played ? ink : with_pixel_size(find_text_style(text, style_scorecard_row_pending), pixel);
    const std::string hole = hole_label(text, row, compact);
    const float hole_pixel = fitted_pixel_size(text.font, ink, hole, glm::vec2((x_edges[1] - x_edges[0]) * 0.48f, 0.030f));
    draw_text(batch,
              text.font,
              with_align(with_pixel_size(ink, hole_pixel), text_align::left),
              hole,
              left_cell_anchor(text.font, x_edges[0] + 0.014f, y, hole_pixel));
    draw_text(batch, text.font, ink, std::to_string(row.par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, score_ink, score_label(row), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, score_ink, relative_label(row), glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_totals(overlay_batch& batch,
                           const text_assets& text,
                           const scorecard_data& scorecard,
                           const std::array<float, 5>& x_edges,
                           const float y,
                           const text_style& style) {
    draw_text(batch,
              text.font,
              with_align(style, text_align::left),
              lookup_text(text, text_scorecard_total),
              left_cell_anchor(text.font, x_edges[0] + 0.014f, y, style.pixel_size));
    draw_text(batch, text.font, style, std::to_string(scorecard.total_par), glm::vec2((x_edges[1] + x_edges[2]) * 0.5f, y));
    draw_text(batch, text.font, style, std::to_string(scorecard.total_strokes), glm::vec2((x_edges[2] + x_edges[3]) * 0.5f, y));
    draw_text(batch, text.font, style, scorecard.total_relative_label, glm::vec2((x_edges[3] + x_edges[4]) * 0.5f, y));
}

void draw_scorecard_card(overlay_batch& batch,
                         const text_assets& text,
                         const scorecard_data& scorecard,
                         const glm::vec2 center,
                         const glm::vec2 half_size,
                         const bool compact) {
    if (scorecard.rows.empty()) {
        return;
    }

    draw_paper_card_base(batch, center, half_size, 1.0f);

    draw_text_fitted(batch,
                     text.font,
                     find_text_style(text, compact ? style_scorecard_title_compact : style_scorecard_title),
                     scorecard.course_name,
                     center + glm::vec2(0.0f, half_size.y - 0.070f),
                     glm::vec2(half_size.x * 0.78f, 0.055f));
    draw_text(batch,
              text.font,
              find_text_style(text, compact ? style_scorecard_subtitle_compact : style_scorecard_subtitle),
              lookup_text(text, compact ? text_scorecard_title : text_scorecard_results_title),
              center + glm::vec2(0.0f, half_size.y - (compact ? 0.126f : 0.142f)));

    const std::size_t row_limit = compact ? std::min<std::size_t>(scorecard.rows.size(), 8U) : scorecard.rows.size();
    std::size_t first_row = 0;
    if (compact && scorecard.rows.size() > row_limit) {
        const std::size_t current = std::min(scorecard.current_hole_index, scorecard.rows.size() - 1U);
        const std::size_t preferred = current > 3U ? current - 3U : 0U;
        first_row = std::min(preferred, scorecard.rows.size() - row_limit);
    }

    const float grid_top = center.y + half_size.y - (compact ? 0.180f : 0.220f);
    const float grid_bottom = center.y - half_size.y + (compact ? 0.118f : 0.142f);
    const float available = std::max(0.12f, grid_top - grid_bottom);
    const float row_height = std::min(compact ? 0.056f : 0.060f, available / static_cast<float>(row_limit + 1U));
    const float header_y = grid_top - row_height * 0.50f;
    const float x0 = center.x - half_size.x + 0.052f;
    const float x4 = center.x + half_size.x - 0.052f;
    const float hole_width = compact ? (x4 - x0) * 0.28f : (x4 - x0) * 0.46f;
    const float par_width = (x4 - x0) * 0.16f;
    const float score_width = (x4 - x0) * 0.22f;
    const std::array<float, 5> x_edges{{
        x0,
        x0 + hole_width,
        x0 + hole_width + par_width,
        x0 + hole_width + par_width + score_width,
        x4
    }};

    draw_scorecard_grid_lines(batch, center, half_size, x_edges, header_y, row_height, static_cast<int>(row_limit), 1.0f);
    draw_scorecard_headers(batch,
                           text,
                           x_edges,
                           header_y,
                           find_text_style(text, compact ? style_scorecard_header_compact : style_scorecard_header));

    // Rows shrink with the row height on long courses.
    const float max_row_pixel = find_text_style(text, compact ? style_scorecard_row_compact : style_scorecard_row).pixel_size;
    const float row_pixel = std::min(max_row_pixel, row_height * 0.18f);
    for (std::size_t row_index = 0; row_index < row_limit; ++row_index) {
        const std::size_t source_index = first_row + row_index;
        const scorecard_row& row = scorecard.rows[source_index];
        const float y = header_y - row_height * (static_cast<float>(row_index) + 1.0f);
        const bool current = !scorecard.finished && source_index == scorecard.current_hole_index;
        draw_scorecard_row_text(batch, text, row, x_edges, y, row_pixel, compact, current);
    }

    const float total_y = center.y - half_size.y + (compact ? 0.060f : 0.076f);
    draw_scorecard_totals(batch,
                          text,
                          scorecard,
                          x_edges,
                          total_y,
                          find_text_style(text, compact ? style_scorecard_total_compact : style_scorecard_total));

    if (!compact) {
        draw_text(batch,
                  text.font,
                  find_text_style(text, style_scorecard_hint),
                  lookup_text(text, text_scorecard_results_hint),
                  center + glm::vec2(0.0f, -half_size.y + 0.034f));
    }
}

void draw_compact_scorecard(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_scorecard_card(batch, text, scorecard, glm::vec2(-0.48f, 0.32f), glm::vec2(0.44f, 0.44f), true);
}

void draw_course_results_card(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.015f, 0.013f, 0.012f), 0.70f);
    draw_scorecard_card(batch, text, scorecard, glm::vec2(0.0f, 0.0f), glm::vec2(0.74f, 0.80f), false);
}

void draw_stroke_ticks(overlay_batch& batch, const int stroke_count) {
    const int strokes = std::max(stroke_count, 0);
    const int ten_marks = std::min(strokes / 10, 8);
    for (int i = 0; i < ten_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.07f;
        draw_overlay_quad(batch, glm::vec2(x, 0.86f), glm::vec2(0.026f, 0.08f), glm::vec3(0.92f, 0.70f, 0.18f), 1.0f);
    }

    const int one_marks = strokes % 10;
    for (int i = 0; i < one_marks; ++i) {
        const float x = -0.92f + static_cast<float>(i) * 0.055f;
        draw_overlay_quad(batch, glm::vec2(x, 0.76f), glm::vec2(0.015f, 0.06f), glm::vec3(0.88f, 0.88f, 0.78f), 1.0f);
    }
}
}

skill_icon_id skill_icon_from_name(const std::string& name) {
    if (name == "golf_swing") {
        return skill_icon_id::golf_swing;
    }
    if (name == "smoking") {
        return skill_icon_id::smoking;
    }
    if (name == "fitness") {
        return skill_icon_id::fitness;
    }
    return skill_icon_id::generic;
}

void draw_hud(overlay_batch& batch, const text_assets& text, const render_data& data, const glm::mat4& view_proj) {
    if (data.show_scorecard) {
        draw_compact_scorecard(batch, text, data.scorecard);
    }
    if (data.show_skills_panel) {
        draw_skills_panel(batch, text, data.skills);
    }
    if (data.show_rangefinder) {
        draw_rangefinder_view(batch, text, view_proj, data);
    }
    draw_cart_hud(batch, text, data);
    draw_club_label(batch, text, data.selected_club_label);
    if (data.show_interact_prompt) {
        draw_interact_prompt(batch);
    }
    draw_xp_drops(batch, text, data.xp_drops);
    draw_controls_overlay(batch, text, data.controls);
    if (data.show_power_meter) {
        draw_power_meter(batch, text, data.swing_power);
    }
    draw_stroke_ticks(batch, data.stroke_count);
}

void draw_course_results(overlay_batch& batch, const text_assets& text, const scorecard_data& scorecard) {
    draw_course_results_card(batch, text, scorecard);
}

void draw_debug_text(overlay_batch& batch, const text_assets& text, const render_data& data) {
    draw_fps_counter(batch, text, data.fps_label);
    draw_profile_overlay(batch, text, data.profile_summary);
}
