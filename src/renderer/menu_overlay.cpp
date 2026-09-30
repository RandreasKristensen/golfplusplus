#include "renderer/menu_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>

#include <glm/common.hpp>

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"

namespace {
enum class help_control_icon {
    arrows,
    space,
    shift,
    enter,
    backspace,
    retee,
    key_1,
    key_2
};

// One text line per '\n' in the string, stepping down by nine font pixels.
void draw_help_text_lines(overlay_batch& batch,
                          const text_assets& text,
                          const glm::vec2 top_left,
                          const char* key) {
    constexpr float line_gap = 9.0f;
    const text_style& style = find_text_style(text, style_body);
    std::istringstream lines(lookup_text(text, key));
    std::string line;
    for (int i = 0; std::getline(lines, line); ++i) {
        if (line.empty()) {
            continue;
        }
        draw_text(batch,
                  text.font,
                  style,
                  line,
                  top_left - glm::vec2(0.0f, static_cast<float>(i) * style.pixel_size * line_gap));
    }
}

void draw_help_control_icon(overlay_batch& batch,
                            const text_assets& text,
                            const help_control_icon icon,
                            const glm::vec2 center) {
    const bool is_down = false;
    const glm::vec2 small_half(0.066f, 0.052f);
    const glm::vec2 wide_half(0.138f, 0.052f);
    const glm::vec2 key_half(0.052f, 0.046f);

    switch (icon) {
    case help_control_icon::arrows: {
        const glm::vec2 dpad_half(0.043f, 0.043f);
        const float offset = 0.052f;
        draw_control_button_base(batch, center + glm::vec2(0.0f, offset), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(0.0f, offset), glm::vec2(0.0f, 1.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(-offset, 0.0f), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(-offset, 0.0f), glm::vec2(-1.0f, 0.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(offset, 0.0f), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(offset, 0.0f), glm::vec2(1.0f, 0.0f), dpad_half, is_down);
        draw_control_button_base(batch, center + glm::vec2(0.0f, -offset), dpad_half, is_down);
        draw_arrow_icon(batch, center + glm::vec2(0.0f, -offset), glm::vec2(0.0f, -1.0f), dpad_half, is_down);
        break;
    }
    case help_control_icon::space:
        draw_control_button_base(batch, center, wide_half, is_down);
        draw_space_icon(batch, center, wide_half, is_down);
        break;
    case help_control_icon::shift:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_shift_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::enter:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_enter_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::backspace:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_backspace_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::retee:
        draw_control_button_base(batch, center, small_half, is_down);
        draw_retee_icon(batch, center, small_half, is_down);
        break;
    case help_control_icon::key_1:
        draw_control_button_base(batch, center, key_half, is_down);
        draw_text(batch, text.font, find_text_style(text, style_control_key), lookup_text(text, text_controls_key_1), center);
        break;
    case help_control_icon::key_2:
        draw_control_button_base(batch, center, key_half, is_down);
        draw_text(batch, text.font, find_text_style(text, style_control_key), lookup_text(text, text_controls_key_2), center);
        break;
    }
}

void draw_help_control_row(overlay_batch& batch,
                           const text_assets& text,
                           const help_control_icon icon,
                           const glm::vec2 icon_center,
                           const glm::vec2 label_top_left,
                           const char* key) {
    draw_help_control_icon(batch, text, icon, icon_center);
    draw_help_text_lines(batch, text, label_top_left, key);
}

void draw_startup_help_screen(overlay_batch& batch, const text_assets& text) {
    const glm::vec2 left_icon(-0.66f, 0.0f);
    const glm::vec2 left_label(-0.46f, 0.0f);
    const glm::vec2 right_icon(0.30f, 0.0f);
    const glm::vec2 right_label(0.45f, 0.0f);

    draw_help_control_row(batch, text, help_control_icon::arrows,
                          left_icon + glm::vec2(0.0f, 0.42f), left_label + glm::vec2(0.0f, 0.51f), text_help_arrows);
    draw_help_control_row(batch, text, help_control_icon::space,
                          left_icon + glm::vec2(0.0f, 0.15f), left_label + glm::vec2(0.0f, 0.22f), text_help_space);
    draw_help_control_row(batch, text, help_control_icon::shift,
                          left_icon + glm::vec2(0.0f, -0.22f), left_label + glm::vec2(0.0f, -0.18f), text_help_left_shift);
    draw_help_control_row(batch, text, help_control_icon::shift,
                          left_icon + glm::vec2(0.0f, -0.43f), left_label + glm::vec2(0.0f, -0.39f), text_help_shift);

    draw_help_control_row(batch, text, help_control_icon::enter,
                          right_icon + glm::vec2(0.0f, 0.43f), right_label + glm::vec2(0.0f, 0.50f), text_help_enter);
    draw_help_control_row(batch, text, help_control_icon::backspace,
                          right_icon + glm::vec2(0.0f, 0.22f), right_label + glm::vec2(0.0f, 0.25f), text_help_backspace);
    draw_help_control_row(batch, text, help_control_icon::retee,
                          right_icon + glm::vec2(0.0f, -0.12f), right_label + glm::vec2(0.0f, -0.12f), text_help_retee);
    draw_help_control_row(batch, text, help_control_icon::key_1,
                          right_icon + glm::vec2(0.0f, -0.32f), right_label + glm::vec2(0.0f, -0.30f), text_help_key_1);
    draw_help_control_row(batch, text, help_control_icon::key_2,
                          right_icon + glm::vec2(0.0f, -0.52f), right_label + glm::vec2(0.0f, -0.50f), text_help_key_2);
}

glm::vec3 thumbnail_zone_color(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return glm::vec3(0.20f, 0.62f, 0.24f);
    case material_zone_type::bunker:
        return glm::vec3(0.68f, 0.56f, 0.24f);
    case material_zone_type::water:
        return glm::vec3(0.12f, 0.24f, 0.66f);
    default:
        return glm::vec3(0.28f, 0.30f, 0.28f);
    }
}

glm::vec2 thumbnail_world_point(const glm::vec3& point, const bool rotate_long_axis) {
    if (rotate_long_axis) {
        return glm::vec2(point.z, -point.x);
    }
    return glm::vec2(point.x, point.z);
}

void expand_preview_bounds(const glm::vec2& point, glm::vec2& min_point, glm::vec2& max_point) {
    min_point.x = std::min(min_point.x, point.x);
    min_point.y = std::min(min_point.y, point.y);
    max_point.x = std::max(max_point.x, point.x);
    max_point.y = std::max(max_point.y, point.y);
}

glm::vec2 preview_point(const glm::vec3& point,
                        const glm::vec2& center,
                        const glm::vec2& half_size,
                        const glm::vec2& min_point,
                        const float scale,
                        const bool rotate_long_axis) {
    const glm::vec2 world = thumbnail_world_point(point, rotate_long_axis);
    return center + glm::vec2((world.x - min_point.x) * scale - half_size.x,
                              (world.y - min_point.y) * scale - half_size.y);
}

void draw_hole_thumbnail(overlay_batch& batch,
                         const render_hole_preview& preview,
                         const glm::vec2 center,
                         const glm::vec2 half_size) {
    draw_overlay_quad(batch, center, half_size, glm::vec3(0.028f, 0.034f, 0.030f), 0.96f);
    draw_button_outline(batch, center, half_size, glm::vec3(0.42f, 0.44f, 0.40f), 0.42f);

    glm::vec2 raw_min(preview.tee_position.x, preview.tee_position.z);
    glm::vec2 raw_max = raw_min;
    expand_preview_bounds(glm::vec2(preview.pin_position.x, preview.pin_position.z), raw_min, raw_max);
    for (const glm::vec3& point : preview.control_points) {
        expand_preview_bounds(glm::vec2(point.x, point.z), raw_min, raw_max);
    }
    for (const material_zone& zone : preview.material_zones) {
        if (zone.has_radius) {
            expand_preview_bounds(glm::vec2(zone.center.x + zone.radius, zone.center.z + zone.radius), raw_min, raw_max);
            expand_preview_bounds(glm::vec2(zone.center.x - zone.radius, zone.center.z - zone.radius), raw_min, raw_max);
        }
        if (zone.has_bounds) {
            expand_preview_bounds(glm::vec2(zone.bounds_min.x, zone.bounds_min.z), raw_min, raw_max);
            expand_preview_bounds(glm::vec2(zone.bounds_max.x, zone.bounds_max.z), raw_min, raw_max);
        }
    }

    const bool rotate_long_axis = (raw_max.y - raw_min.y) > (raw_max.x - raw_min.x);
    glm::vec2 min_point = thumbnail_world_point(preview.tee_position, rotate_long_axis);
    glm::vec2 max_point = min_point;
    expand_preview_bounds(thumbnail_world_point(preview.pin_position, rotate_long_axis), min_point, max_point);
    for (const glm::vec3& point : preview.control_points) {
        expand_preview_bounds(thumbnail_world_point(point, rotate_long_axis), min_point, max_point);
    }
    for (const material_zone& zone : preview.material_zones) {
        if (zone.has_radius) {
            expand_preview_bounds(thumbnail_world_point(zone.center + glm::vec3(zone.radius, 0.0f, zone.radius), rotate_long_axis), min_point, max_point);
            expand_preview_bounds(thumbnail_world_point(zone.center - glm::vec3(zone.radius, 0.0f, zone.radius), rotate_long_axis), min_point, max_point);
        }
        if (zone.has_bounds) {
            expand_preview_bounds(thumbnail_world_point(zone.bounds_min, rotate_long_axis), min_point, max_point);
            expand_preview_bounds(thumbnail_world_point(zone.bounds_max, rotate_long_axis), min_point, max_point);
        }
    }

    const glm::vec2 span = glm::max(max_point - min_point, glm::vec2(1.0f));
    const glm::vec2 inset_half = half_size * 0.82f;
    const float scale = std::min((inset_half.x * 2.0f) / span.x, (inset_half.y * 2.0f) / span.y);
    const glm::vec2 padded_min = min_point - (glm::vec2(inset_half.x * 2.0f, inset_half.y * 2.0f) / scale - span) * 0.5f;

    if (preview.control_points.size() >= 2) {
        const float fairway_width = std::max(0.010f, preview.fairway_width * scale * 0.35f);
        for (std::size_t i = 1; i < preview.control_points.size(); ++i) {
            const glm::vec2 a = preview_point(preview.control_points[i - 1], center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(preview.control_points[i], center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_segment(batch, a, b, fairway_width, glm::vec3(0.18f, 0.46f, 0.18f), 0.82f);
            draw_overlay_segment(batch, a, b, 0.006f, glm::vec3(0.62f, 0.78f, 0.38f), 0.55f);
        }
    }

    for (const material_zone& zone : preview.material_zones) {
        const glm::vec3 color = thumbnail_zone_color(zone.type);
        if (zone.has_bounds) {
            const glm::vec2 a = preview_point(zone.bounds_min, center, inset_half, padded_min, scale, rotate_long_axis);
            const glm::vec2 b = preview_point(zone.bounds_max, center, inset_half, padded_min, scale, rotate_long_axis);
            draw_overlay_quad(batch, (a + b) * 0.5f, glm::abs(b - a) * 0.5f, color, 0.64f);
        } else if (zone.has_radius) {
            const glm::vec2 p = preview_point(zone.center, center, inset_half, padded_min, scale, rotate_long_axis);
            const float radius = std::max(0.010f, zone.radius * scale);
            draw_overlay_quad(batch, p, glm::vec2(radius), color, 0.64f);
        }
    }

    const glm::vec2 tee = preview_point(preview.tee_position, center, inset_half, padded_min, scale, rotate_long_axis);
    const glm::vec2 pin = preview_point(preview.pin_position, center, inset_half, padded_min, scale, rotate_long_axis);
    draw_overlay_quad(batch, tee, glm::vec2(0.014f), glm::vec3(0.88f, 0.80f, 0.48f), 0.94f);
    draw_overlay_quad(batch, pin, glm::vec2(0.012f, 0.028f), glm::vec3(0.88f, 0.18f, 0.12f), 0.94f);
}
}

glm::vec2 startup_tile_center(const startup_menu_screen screen, const int index) {
    if (screen == startup_menu_screen::main) {
        return glm::vec2(0.0f, 0.26f - static_cast<float>(index) * 0.24f);
    }

    constexpr int columns = 3;
    const int row = index / columns;
    const int column = index % columns;
    return glm::vec2(-0.58f + static_cast<float>(column) * 0.58f,
                     0.36f - static_cast<float>(row) * 0.38f);
}

glm::vec2 startup_tile_half_size(const startup_menu_screen screen) {
    return screen == startup_menu_screen::main ? glm::vec2(0.42f, 0.095f) : glm::vec2(0.25f, 0.165f);
}

int startup_tile_at(const startup_menu_screen screen, const int count, const glm::vec2 point) {
    const glm::vec2 half = startup_tile_half_size(screen);
    for (int i = 0; i < count; ++i) {
        const glm::vec2 center = startup_tile_center(screen, i);
        if (std::abs(point.x - center.x) <= half.x && std::abs(point.y - center.y) <= half.y) {
            return i;
        }
    }
    return -1;
}

void draw_startup_menu(overlay_batch& batch, const text_assets& text, const render_startup_menu& menu) {
    if (menu.screen == startup_menu_screen::none) {
        return;
    }

    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.0f, 0.0f, 0.0f), 0.72f);
    draw_overlay_quad(batch, glm::vec2(0.0f, 0.0f), glm::vec2(0.86f, 0.88f), glm::vec3(0.012f, 0.014f, 0.014f), 0.30f);
    draw_text_fitted(batch, text.font, find_text_style(text, style_title), menu.title, glm::vec2(0.0f, 0.80f), glm::vec2(0.78f, 0.10f));
    if (!menu.subtitle.empty()) {
        draw_text_fitted(batch, text.font, find_text_style(text, style_subtitle), menu.subtitle, glm::vec2(0.0f, 0.68f), glm::vec2(0.70f, 0.065f));
    }

    if (menu.screen == startup_menu_screen::help) {
        draw_startup_help_screen(batch, text);
    }

    const glm::vec2 tile_half = startup_tile_half_size(menu.screen);
    for (std::size_t i = 0; i < menu.tiles.size(); ++i) {
        const render_startup_tile& tile = menu.tiles[i];
        const glm::vec2 center = startup_tile_center(menu.screen, static_cast<int>(i));
        const glm::vec3 panel_color = tile.selected ? glm::vec3(0.20f, 0.16f, 0.065f) : glm::vec3(0.070f, 0.075f, 0.075f);
        const glm::vec3 outline = tile.selected ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);
        draw_overlay_quad(batch, center, tile_half, panel_color, tile.selected ? 0.94f : 0.76f);
        draw_button_outline(batch, center, tile_half, outline, tile.selected ? 0.94f : 0.44f);

        if (tile.has_preview) {
            draw_hole_thumbnail(batch, tile.preview, center + glm::vec2(0.0f, 0.030f), glm::vec2(tile_half.x * 0.86f, tile_half.y * 0.48f));

            draw_text_fitted(batch,
                             text.font,
                             find_text_style(text, style_tile_label_small),
                             tile.title,
                             center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.28f),
                             glm::vec2(tile_half.x * 0.86f, tile_half.y * 0.22f));
            draw_text_fitted(batch,
                             text.font,
                             find_text_style(text, style_tile_hint_small),
                             tile.subtitle,
                             center + glm::vec2(-tile_half.x * 0.86f, -tile_half.y * 0.58f),
                             glm::vec2(tile_half.x * 0.86f, tile_half.y * 0.18f));
        } else {
            draw_text_fitted(batch,
                             text.font,
                             find_text_style(text, style_tile_label),
                             tile.title,
                             center + glm::vec2(0.0f, 0.018f),
                             glm::vec2(tile_half.x * 0.82f, tile_half.y * 0.32f));
            draw_text_fitted(batch,
                             text.font,
                             find_text_style(text, style_tile_hint),
                             tile.subtitle,
                             center + glm::vec2(0.0f, -0.050f),
                             glm::vec2(tile_half.x * 0.82f, tile_half.y * 0.26f));
        }
    }

    if (!menu.footer.empty()) {
        draw_text_fitted(batch, text.font, find_text_style(text, style_footer), menu.footer, glm::vec2(0.0f, -0.86f), glm::vec2(0.78f, 0.05f));
    }
}
