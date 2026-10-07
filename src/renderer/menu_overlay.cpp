#include "renderer/menu_overlay.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include <glm/common.hpp>

#include "game/text_ids.h"
#include "renderer/control_icons.h"
#include "renderer/pixel_font.h"
#include "renderer/terrain_palette.h"

namespace {
enum class help_control_icon {
    arrows,
    space,
    shift,
    enter,
    backspace,
    retee,
    key_1,
    key_2,
    key_g
};

void draw_key_cap(overlay_batch& batch, const text_assets& text, const char* key, const glm::vec2 center, const glm::vec2 half_size) {
    draw_control_button_base(batch, center, half_size, false);
    draw_label(batch, text.font, find_text_style(text, style_control_key), lookup_text(text, key), ui_rect{center, half_size});
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
        draw_key_cap(batch, text, text_controls_key_1, center, key_half);
        break;
    case help_control_icon::key_2:
        draw_key_cap(batch, text, text_controls_key_2, center, key_half);
        break;
    case help_control_icon::key_g:
        draw_key_cap(batch, text, text_controls_key_g, center, key_half);
        break;
    }
}

struct help_row {
    help_control_icon icon;
    glm::vec2 icon_center;
    // The label's box runs from here down to the next row's label.
    float label_top;
    const char* key;
};

// A column's labels sit between label_left and label_right.
struct help_column {
    float label_left;
    float label_right;
    std::vector<help_row> rows;
};

constexpr float help_column_bottom = -0.80f;
constexpr float help_row_gap = 0.02f;

ui_rect help_label_box(const help_column& column, const std::size_t row) {
    const float bottom = row + 1 < column.rows.size() ? column.rows[row + 1].label_top + help_row_gap : help_column_bottom;
    return rect_from_edges(column.label_left, bottom, column.label_right, column.rows[row].label_top);
}

void draw_startup_help_screen(overlay_batch& batch, const text_assets& text) {
    const std::vector<help_column> columns{
        {-0.46f, 0.14f, {
            {help_control_icon::arrows, glm::vec2(-0.66f, 0.42f), 0.51f, text_help_arrows},
            {help_control_icon::space, glm::vec2(-0.66f, 0.15f), 0.22f, text_help_space},
            {help_control_icon::shift, glm::vec2(-0.66f, -0.22f), -0.18f, text_help_left_shift},
            {help_control_icon::shift, glm::vec2(-0.66f, -0.43f), -0.39f, text_help_shift},
        }},
        {0.45f, 0.84f, {
            {help_control_icon::enter, glm::vec2(0.30f, 0.43f), 0.50f, text_help_enter},
            {help_control_icon::backspace, glm::vec2(0.30f, 0.22f), 0.25f, text_help_backspace},
            {help_control_icon::retee, glm::vec2(0.30f, -0.12f), -0.12f, text_help_retee},
            {help_control_icon::key_1, glm::vec2(0.30f, -0.32f), -0.30f, text_help_key_1},
            {help_control_icon::key_2, glm::vec2(0.30f, -0.48f), -0.46f, text_help_key_2},
            {help_control_icon::key_g, glm::vec2(0.30f, -0.66f), -0.62f, text_help_key_g},
        }},
    };

    // One shared size, set by the label with the least room.
    const text_style& style = find_text_style(text, style_body);
    int scale = 0;
    for (const help_column& column : columns) {
        for (std::size_t i = 0; i < column.rows.size(); ++i) {
            const int fits = fit_text_scale(text.font, style, lookup_text(text, column.rows[i].key), help_label_box(column, i), batch.grid);
            scale = scale == 0 ? fits : std::min(scale, fits);
        }
    }
    scale = std::max(scale, style.min_scale);

    for (const help_column& column : columns) {
        for (std::size_t i = 0; i < column.rows.size(); ++i) {
            const help_row& row = column.rows[i];
            draw_help_control_icon(batch, text, row.icon, row.icon_center);
            draw_text_layout(batch,
                             text.font,
                             layout_text_at_scale(text.font, style, lookup_text(text, row.key), help_label_box(column, i), batch.grid, scale),
                             style.color);
        }
    }
}

glm::vec3 thumbnail_zone_color(const material_zone_type type) {
    switch (type) {
    case material_zone_type::green:
        return terrain_material_color(terrain_material::green);
    case material_zone_type::bunker:
        return terrain_material_color(terrain_material::bunker);
    case material_zone_type::water:
        return terrain_material_color(terrain_material::water);
    case material_zone_type::unknown:
        break;
    }
    return terrain_material_color(terrain_material::rough);
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
        const glm::vec2 extent = zone_half_extent(zone);
        expand_preview_bounds(glm::vec2(zone.center.x, zone.center.z) + extent, raw_min, raw_max);
        expand_preview_bounds(glm::vec2(zone.center.x, zone.center.z) - extent, raw_min, raw_max);
    }

    const bool rotate_long_axis = (raw_max.y - raw_min.y) > (raw_max.x - raw_min.x);
    glm::vec2 min_point = thumbnail_world_point(preview.tee_position, rotate_long_axis);
    glm::vec2 max_point = min_point;
    expand_preview_bounds(thumbnail_world_point(preview.pin_position, rotate_long_axis), min_point, max_point);
    for (const glm::vec3& point : preview.control_points) {
        expand_preview_bounds(thumbnail_world_point(point, rotate_long_axis), min_point, max_point);
    }
    for (const material_zone& zone : preview.material_zones) {
        const glm::vec2 extent = zone_half_extent(zone);
        const glm::vec3 corner(extent.x, 0.0f, extent.y);
        expand_preview_bounds(thumbnail_world_point(zone.center + corner, rotate_long_axis), min_point, max_point);
        expand_preview_bounds(thumbnail_world_point(zone.center - corner, rotate_long_axis), min_point, max_point);
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
            draw_overlay_segment(batch, a, b, fairway_width, terrain_material_color(terrain_material::fairway), 0.82f);
            draw_overlay_segment(batch, a, b, 0.006f, glm::vec3(0.62f, 0.78f, 0.38f), 0.55f);
        }
    }

    for (const material_zone& zone : preview.material_zones) {
        // Each zone as the rectangle around it, at least a dot across.
        const glm::vec3 color = thumbnail_zone_color(zone.type);
        const glm::vec2 extent = zone_half_extent(zone);
        const glm::vec2 drawn = rotate_long_axis ? glm::vec2(extent.y, extent.x) : extent;
        const glm::vec2 p = preview_point(zone.center, center, inset_half, padded_min, scale, rotate_long_axis);
        draw_overlay_quad(batch, p, glm::max(drawn * scale, glm::vec2(0.010f)), color, 0.64f);
    }

    const glm::vec2 tee = preview_point(preview.tee_position, center, inset_half, padded_min, scale, rotate_long_axis);
    const glm::vec2 pin = preview_point(preview.pin_position, center, inset_half, padded_min, scale, rotate_long_axis);
    draw_overlay_quad(batch, tee, glm::vec2(0.014f), glm::vec3(0.88f, 0.80f, 0.48f), 0.94f);
    draw_overlay_quad(batch, pin, glm::vec2(0.012f, 0.028f), glm::vec3(0.88f, 0.18f, 0.12f), 0.94f);
}

bool single_column(const startup_menu_screen screen) {
    return screen == startup_menu_screen::main || screen == startup_menu_screen::confirm ||
        screen == startup_menu_screen::form;
}

bool picker(const startup_menu_screen screen) {
    return screen == startup_menu_screen::hole_picker || screen == startup_menu_screen::course_picker;
}

// A single column runs down from its top edge to above the footer. Main
// and confirm columns start under the subtitle; form columns under the form.
constexpr float column_top = 0.355f;
constexpr float form_column_top = -0.125f;
constexpr float column_bottom = -0.775f;
constexpr float column_pitch = 0.24f;
constexpr float column_half_width = 0.42f;
constexpr float column_half_height = 0.095f;
// The least space between two tiles when they close up.
constexpr float column_min_gap = 0.02f;

// Tile spacing: the usual pitch, or less when that many tiles would run
// past the bottom. Tiles shrink with it, giving up their gap first.
float column_pitch_for(const startup_menu_screen screen, const int count) {
    if (screen == startup_menu_screen::form || count <= 1) {
        return column_pitch;
    }
    return std::min(column_pitch, (column_top - column_bottom + column_min_gap) / static_cast<float>(count));
}

// The form screens' message line, field and code, above their tiles.
const ui_rect message_box{glm::vec2(0.0f, 0.49f), glm::vec2(0.72f, 0.06f)};
const ui_rect field_box{glm::vec2(0.0f, 0.20f), glm::vec2(0.42f, 0.09f)};
const ui_rect code_box{glm::vec2(0.0f, 0.20f), glm::vec2(0.50f, 0.14f)};
}

glm::vec2 startup_tile_center(const startup_menu_screen screen, const int index, const int count) {
    if (single_column(screen)) {
        const float top = screen == startup_menu_screen::form ? form_column_top : column_top;
        const float pitch = column_pitch_for(screen, count);
        return glm::vec2(0.0f, top - startup_tile_half_size(screen, count).y - static_cast<float>(index) * pitch);
    }

    constexpr int columns = 3;
    const int row = index / columns;
    const int column = index % columns;
    return glm::vec2(-0.58f + static_cast<float>(column) * 0.58f,
                     0.36f - static_cast<float>(row) * 0.38f);
}

glm::vec2 startup_tile_half_size(const startup_menu_screen screen, const int count) {
    if (!single_column(screen)) {
        return glm::vec2(0.25f, 0.165f);
    }
    return glm::vec2(column_half_width,
                     std::min(column_half_height, 0.5f * (column_pitch_for(screen, count) - column_min_gap)));
}

int startup_tile_at(const startup_menu_screen screen, const int count, const glm::vec2 point) {
    const glm::vec2 half = startup_tile_half_size(screen, count);
    for (int i = 0; i < count; ++i) {
        const glm::vec2 center = startup_tile_center(screen, i, count);
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
    draw_label(batch, text.font, find_text_style(text, style_title), menu.title, ui_rect{glm::vec2(0.0f, 0.80f), glm::vec2(0.78f, 0.10f)});
    const ui_rect subtitle_box{glm::vec2(0.0f, 0.66f), glm::vec2(0.70f, 0.045f)};
    const text_style& message_style = find_text_style(text, menu.message_is_error ? style_error : style_subtitle);
    if (picker(menu.screen) && !menu.message.empty()) {
        draw_label(batch, text.font, message_style, menu.message, subtitle_box);
    } else {
        draw_label(batch, text.font, find_text_style(text, style_subtitle), menu.subtitle, subtitle_box);
        if (!menu.message.empty()) {
            draw_label(batch, text.font, message_style, menu.message, message_box);
        }
    }
    if (menu.field) {
        draw_text_input(batch, text.font, find_text_style(text, style_input), *menu.field, field_box, menu.cursor_time);
    }
    if (!menu.code.empty()) {
        draw_label(batch, text.font, find_text_style(text, style_link_code), menu.code, code_box);
    }

    if (menu.screen == startup_menu_screen::help) {
        draw_startup_help_screen(batch, text);
    }

    const int tile_count = static_cast<int>(menu.tiles.size());
    const glm::vec2 tile_half = startup_tile_half_size(menu.screen, tile_count);
    for (std::size_t i = 0; i < menu.tiles.size(); ++i) {
        const render_startup_tile& tile = menu.tiles[i];
        const ui_rect tile_box{startup_tile_center(menu.screen, static_cast<int>(i), tile_count), tile_half};
        const glm::vec3 panel_color = tile.selected ? glm::vec3(0.20f, 0.16f, 0.065f) : glm::vec3(0.070f, 0.075f, 0.075f);
        const glm::vec3 outline = tile.selected ? glm::vec3(0.94f, 0.72f, 0.22f) : glm::vec3(0.50f, 0.52f, 0.48f);
        draw_overlay_quad(batch, tile_box.center, tile_box.half_size, panel_color, tile.selected ? 0.94f : 0.76f);
        draw_button_outline(batch, tile_box.center, tile_box.half_size, outline, tile.selected ? 0.94f : 0.44f);

        const ui_rect inner = inset_rect(tile_box, tile_half * glm::vec2(0.08f, 0.10f));
        if (tile.preview) {
            const ui_rect thumbnail = slice_y(inner, 0.0f, 0.52f);
            draw_hole_thumbnail(batch, *tile.preview, thumbnail.center, thumbnail.half_size);
            draw_label(batch, text.font, find_text_style(text, style_tile_label_small), tile.title, slice_y(inner, 0.54f, 0.82f));
            draw_label(batch, text.font, find_text_style(text, style_tile_hint_small), tile.subtitle, slice_y(inner, 0.82f, 1.0f));
        } else {
            draw_label(batch, text.font, find_text_style(text, style_tile_label), tile.title, slice_y(inner, 0.0f, 0.64f));
            draw_label(batch, text.font, find_text_style(text, style_tile_hint), tile.subtitle, slice_y(inner, 0.64f, 1.0f));
        }
    }

    draw_label(batch, text.font, find_text_style(text, style_footer), menu.footer, ui_rect{glm::vec2(0.0f, -0.86f), glm::vec2(0.78f, 0.05f)});
}

void draw_loading_screen(overlay_batch& batch, const text_assets& text) {
    draw_label(batch, text.font, find_text_style(text, style_title), lookup_text(text, text_menu_main_title),
               ui_rect{glm::vec2(0.0f, 0.14f), glm::vec2(0.78f, 0.16f)});
    draw_label(batch, text.font, find_text_style(text, style_subtitle), lookup_text(text, text_menu_loading),
               ui_rect{glm::vec2(0.0f, -0.12f), glm::vec2(0.70f, 0.045f)});
}
