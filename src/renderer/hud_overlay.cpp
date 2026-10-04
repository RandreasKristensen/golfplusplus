#include "renderer/hud_overlay.h"

#include "renderer/online_overlay.h"

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"
#include "renderer/scorecard_overlay.h"
#include "renderer/ui_rect.h"

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
    draw_label(batch,
               text.font,
               find_text_style(text, is_down ? style_control_key_down : style_control_key),
               lookup_text(text, key),
               ui_rect{center, half_size});
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
    if (controls.show_group_key) {
        draw_control_key(batch, text, text_controls_key_g, glm::vec2(0.79f, -0.87f), key_half, controls.group_down);
    }
}

void draw_fps_counter(overlay_batch& batch, const text_assets& text, const std::string& label) {
    draw_label(batch, text.font, find_text_style(text, style_debug_fps), label, rect_from_edges(-0.97f, 0.86f, 0.30f, 0.98f));
}

// Developer diagnostics: the lines come from profiling, not the string table.
void draw_profile_overlay(overlay_batch& batch, const text_assets& text, const frame_profile& profile) {
    std::string lines;
    for (const std::string& line : format_profile_overlay_lines(profile)) {
        lines += lines.empty() ? line : "\n" + line;
    }
    draw_label(batch, text.font, find_text_style(text, style_debug_profile), lines, rect_from_edges(-0.97f, -0.95f, 0.95f, 0.85f));
}

void draw_club_label(overlay_batch& batch, const text_assets& text, const std::string& label) {
    const glm::vec3 panel_color(0.055f, 0.06f, 0.07f);
    const ui_rect panel{glm::vec2(0.78f, 0.78f), glm::vec2(0.17f, 0.12f)};
    draw_overlay_quad(batch, panel.center, panel.half_size, panel_color);

    draw_label(batch, text.font, find_text_style(text, style_hud_club), label, inset_rect(panel, glm::vec2(0.02f)));
}

// Top centre, clear of the stroke ticks, cart panel, club and XP drops.
void draw_mode_label(overlay_batch& batch, const text_assets& text, const std::string& label) {
    draw_label(batch, text.font, find_text_style(text, style_hud_mode), label, rect_from_edges(-0.27f, 0.85f, 0.27f, 0.97f));
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

    draw_label(batch,
               text.font,
               find_text_style(text, style_hud_label),
               lookup_text(text, text_hud_power),
               rect_from_edges(panel_center.x - 0.30f, panel_center.y + 0.055f, panel_center.x + 0.10f, panel_center.y + 0.145f));

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

    // Tick labels sit under their ticks, between the tick ends and the panel edge.
    const text_style& scale_style = find_text_style(text, style_hud_scale);
    const glm::vec2 label_half(0.05f, 0.028f);
    const float label_y = panel_center.y - 0.122f;
    draw_label(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_min), ui_rect{glm::vec2(track_left, label_y), label_half});
    draw_label(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_mid), ui_rect{glm::vec2(track_center.x, label_y), label_half});
    draw_label(batch, text.font, scale_style, lookup_text(text, text_hud_power_tick_max), ui_rect{glm::vec2(track_right, label_y), label_half});
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

    const ui_rect text_area = rect_from_edges(panel_center.x - 0.17f, panel_center.y - 0.05f, panel_center.x + 0.17f, panel_center.y + 0.12f);
    draw_label(batch, text.font, find_text_style(text, style_cart_label), lookup_text(text, text_hud_cart), rect_row(text_area, 0, 2));
    draw_label(batch,
               text.font,
               find_text_style(text, data.cart_drifting ? style_cart_drift : style_cart_drive),
               lookup_text(text, data.cart_drifting ? text_hud_cart_drift : text_hud_cart_drive),
               rect_row(text_area, 1, 2));

    const glm::vec2 track_center = panel_center + glm::vec2(0.0f, -0.085f);
    const glm::vec2 track_half(0.150f, 0.024f);
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

    const ui_rect panel{glm::vec2(0.0f, 0.16f), glm::vec2(0.58f, 0.44f)};
    const glm::vec3 panel_color(0.050f, 0.055f, 0.060f);
    const glm::vec3 outline_color(0.62f, 0.64f, 0.60f);
    const text_style& header = find_text_style(text, style_panel_header);
    const text_style& value = find_text_style(text, style_panel_value);

    draw_overlay_quad(batch, panel.center + glm::vec2(0.018f, -0.020f), panel.half_size, glm::vec3(0.0f), 0.32f);
    draw_overlay_quad(batch, panel.center, panel.half_size, panel_color, 0.88f);
    draw_button_outline(batch, panel.center, panel.half_size, outline_color, 0.64f);

    const ui_rect inner = inset_rect(panel, glm::vec2(0.08f, 0.0f));
    draw_label(batch, text.font, find_text_style(text, style_panel_title), lookup_text(text, text_skills_title), slice_y(inner, 0.01f, 0.155f));

    // Columns: name, level, xp, xp to next level.
    const std::array<float, 5> columns{{0.0f, 0.50f, 0.64f, 0.84f, 1.0f}};
    const auto cell = [&columns](const ui_rect& row, const std::size_t column) {
        return slice_x(row, columns[column], columns[column + 1]);
    };

    const ui_rect header_row = slice_y(inner, 0.16f, 0.23f);
    const std::array<const char*, 4> header_keys{{text_skills_header_name, text_skills_header_level, text_skills_header_xp, text_skills_header_next}};
    for (std::size_t column = 0; column < header_keys.size(); ++column) {
        draw_label(batch, text.font, header, lookup_text(text, header_keys[column]), cell(header_row, column));
    }

    constexpr int min_rows = 5;
    const ui_rect rows = slice_y(inner, 0.24f, 0.86f);
    const int row_count = std::max(min_rows, static_cast<int>(skills.size()));
    for (std::size_t i = 0; i < skills.size(); ++i) {
        const render_skill_progress& skill = skills[i];
        const ui_rect row = rect_row(rows, static_cast<int>(i), row_count);
        const glm::vec3 row_color = i % 2 == 0 ? glm::vec3(0.075f, 0.080f, 0.078f) : glm::vec3(0.060f, 0.064f, 0.066f);
        const ui_rect band = inset_rect(row, glm::vec2(0.0f, row.half_size.y * 0.08f));
        draw_overlay_quad(batch, band.center, band.half_size, row_color, 0.74f);
        draw_label(batch, text.font, value, skill.label, cell(band, 0));
        draw_label(batch, text.font, value, std::to_string(std::max(1, skill.level)), cell(band, 1));
        draw_label(batch, text.font, value, std::to_string(std::max(0, skill.xp)), cell(band, 2));
        draw_label(batch, text.font, value, std::to_string(std::max(0, skill.xp_to_next)), cell(band, 3));
    }

    draw_label(batch, text.font, find_text_style(text, style_panel_hint), lookup_text(text, text_skills_hint), slice_y(inner, 0.88f, 0.99f));
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
        draw_label(batch,
                   text.font,
                   with_color(label_style, label_style.color * alpha),
                   label,
                   rect_from_edges(center.x - 0.075f, center.y - row_half.y + 0.004f, center.x + row_half.x - 0.008f, center.y + row_half.y - 0.004f));
    }
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
    // The panel is sized to the laid-out label, so lay it out first.
    const text_style& style = find_text_style(text, style_rangefinder);
    const ui_rect label_box{pin_screen + glm::vec2(0.0f, 0.015f), glm::vec2(0.22f, 0.065f)};
    const text_layout label = layout_text(text.font, style, data.rangefinder_label, label_box, batch.grid);
    const glm::vec2 panel_half(std::max(0.12f, label.bounds.half_size.x + 0.035f), 0.070f);
    draw_overlay_quad(batch, label_box.center, panel_half, glm::vec3(0.015f, 0.032f, 0.024f), 0.78f);
    draw_text_layout(batch, text.font, label, style.color);
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
    draw_name_tags(batch, text, data.name_tags, view_proj);
    if (data.show_scorecard) {
        draw_compact_scorecard(batch, text, data.scorecard);
        draw_group_scorecard(batch, text, data.group_scorecard);
    }
    if (data.show_skills_panel) {
        draw_skills_panel(batch, text, data.skills);
    }
    if (data.show_rangefinder) {
        draw_rangefinder_view(batch, text, view_proj, data);
    }
    draw_mode_label(batch, text, data.mode_label);
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
    draw_notice(batch, text, data.notice_label);
}

void draw_debug_text(overlay_batch& batch, const text_assets& text, const render_data& data) {
    draw_fps_counter(batch, text, data.fps_label);
    draw_profile_overlay(batch, text, data.profile_summary);
}
