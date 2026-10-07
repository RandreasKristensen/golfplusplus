#include "renderer/hole_sign_face.h"

#include "game/text_ids.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"
#include "renderer/course_map_fill.h"
#include "renderer/pixel_font.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
const glm::vec3 panel_paint(0.09f, 0.20f, 0.12f);

// Face layout in texels from the top-left corner: the painted panel inside a
// wood frame, the number on top, the map on paper (hole_sign_map_*), par and
// length side by side below.
constexpr int frame_texels = 3;
constexpr int number_top = 4;
constexpr int number_height = 11;
constexpr int detail_top = 52;
constexpr int detail_height = 9;
// Empty texels between the hole's extent and the map box's edges.
constexpr float map_margin_texels = 2.0f;
// Points around each zone's outline when fitting the map to it.
constexpr int zone_outline_points = 16;

// A whole-texel rectangle on the face: columns from the left, rows from the
// top (as hole_sign_texels), clipped to `clip`.
void fill_texels(overlay_batch& batch, int left, int top, int width, int height, const ui_rect& clip, const glm::vec3 color) {
    const ui_rect rect = hole_sign_texels(left, top, width, height);
    const float x0 = std::max(rect_left(rect), rect_left(clip));
    const float x1 = std::min(rect_right(rect), rect_right(clip));
    const float y0 = std::max(rect_bottom(rect), rect_bottom(clip));
    const float y1 = std::min(rect_top(rect), rect_top(clip));
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    draw_overlay_quad(batch, glm::vec2((x0 + x1) * 0.5f, (y0 + y1) * 0.5f), glm::vec2((x1 - x0) * 0.5f, (y1 - y0) * 0.5f), color);
}

const terrain_mesh* own_ribbon(const play_area& area, const hole_sign& sign) {
    return sign.area_hole < area.holes.size() ? &area.holes[sign.area_hole] : nullptr;
}

// Each map row as runs of equal ink, one quad per run; the paper shows off
// the hole.
void draw_hole_map_ground(overlay_batch& batch, const play_area& area, const hole_sign& sign, const hole_sign_map_frame& frame,
                          const ui_rect& clip) {
    for (int row = 0; row < hole_sign_map_height; ++row) {
        int run_start = 0;
        glm::vec3 run_ink = course_map_paper;
        for (int column = 0; column <= hole_sign_map_width; ++column) {
            glm::vec3 ink = run_ink;
            if (column < hole_sign_map_width) {
                const glm::vec3 point = frame.world(static_cast<float>(column) + 0.5f, static_cast<float>(row) + 0.5f);
                const std::optional<terrain_material> material = hole_sign_map_material(area, sign, point);
                ink = material ? course_map_material_ink(*material) : course_map_paper;
            }
            if (column == hole_sign_map_width || (column > run_start && ink != run_ink)) {
                if (run_ink != course_map_paper) {
                    fill_texels(batch, hole_sign_map_left + run_start, hole_sign_map_top + row, column - run_start, 1, clip, run_ink);
                }
                run_start = column;
            }
            run_ink = ink;
        }
    }
}

void draw_hole_map_marks(overlay_batch& batch, const hole_sign& sign, const hole_sign_map_frame& frame, const ui_rect& clip) {
    if (sign.line_of_play.empty()) {
        return;
    }
    const auto mark = [&](const glm::ivec2 texel, const int width, const int height, const glm::vec3 color) {
        fill_texels(batch, hole_sign_map_left + texel.x, hole_sign_map_top + texel.y, width, height, clip, color);
    };
    mark(frame.texel(sign.line_of_play.front()) - glm::ivec2(1, 1), 2, 2, course_map_tee_ink);
    const glm::ivec2 pin = frame.texel(sign.line_of_play.back());
    mark(pin - glm::ivec2(0, 3), 1, 4, course_map_flagstick_ink);
    mark(pin + glm::ivec2(1, -3), 2, 2, course_map_flag_ink);
}
}

glm::vec3 hole_sign_map_frame::world(const float column, const float row) const {
    const float towards_pin = (static_cast<float>(hole_sign_map_width) * 0.5f - column) / texels_per_unit;
    const float across = (static_cast<float>(hole_sign_map_height) * 0.5f - row) / texels_per_unit;
    return origin + to_pin * towards_pin + far_side * across;
}

glm::ivec2 hole_sign_map_frame::texel(const glm::vec3& point) const {
    const glm::vec3 offset = horizontal(point - origin);
    const float column = static_cast<float>(hole_sign_map_width) * 0.5f - glm::dot(offset, to_pin) * texels_per_unit;
    const float row = static_cast<float>(hole_sign_map_height) * 0.5f - glm::dot(offset, far_side) * texels_per_unit;
    return glm::ivec2(static_cast<int>(std::floor(column)), static_cast<int>(std::floor(row)));
}

hole_sign_map_frame make_hole_sign_map_frame(const play_area& area, const hole_sign& sign) {
    hole_sign_map_frame frame;
    if (sign.line_of_play.empty()) {
        return frame;
    }
    const glm::vec3 tee = sign.line_of_play.front();
    frame.to_pin = safe_normalize(horizontal(sign.line_of_play.back() - tee), sign.down_hole);
    // The hole's right, looking from the tee to the pin.
    frame.far_side = glm::vec3(-frame.to_pin.z, 0.0f, frame.to_pin.x);

    glm::vec2 low(std::numeric_limits<float>::max());
    glm::vec2 high(std::numeric_limits<float>::lowest());
    const auto cover = [&](const glm::vec3& point) {
        const glm::vec3 offset = horizontal(point - tee);
        const glm::vec2 local(glm::dot(offset, frame.to_pin), glm::dot(offset, frame.far_side));
        low = glm::min(low, local);
        high = glm::max(high, local);
    };
    const terrain_mesh* ribbon = own_ribbon(area, sign);
    if (ribbon != nullptr && !ribbon->vertices.empty()) {
        for (const terrain_vertex& vertex : ribbon->vertices) {
            cover(vertex.position);
        }
    } else {
        for (const glm::vec3& point : sign.line_of_play) {
            cover(point + frame.far_side * sign.half_width);
            cover(point - frame.far_side * sign.half_width);
        }
    }
    for (const material_zone& zone : sign.zones) {
        if (zone.type != material_zone_type::green && zone.type != material_zone_type::bunker) {
            continue;
        }
        for (int i = 0; i < zone_outline_points; ++i) {
            const float angle = glm::two_pi<float>() * static_cast<float>(i) / static_cast<float>(zone_outline_points);
            cover(zone_world_point(zone, glm::vec2(std::cos(angle), std::sin(angle)) * zone.radii));
        }
    }

    const glm::vec2 span = glm::max(high - low, glm::vec2(1.0f));
    const glm::vec2 middle = (low + high) * 0.5f;
    frame.origin = tee + frame.to_pin * middle.x + frame.far_side * middle.y;
    frame.texels_per_unit = std::min((static_cast<float>(hole_sign_map_width) - map_margin_texels * 2.0f) / span.x,
                                     (static_cast<float>(hole_sign_map_height) - map_margin_texels * 2.0f) / span.y);
    return frame;
}

std::optional<terrain_material> hole_sign_map_material(const play_area& area, const hole_sign& sign, const glm::vec3& point) {
    if (const std::optional<terrain_material> zone = zone_material_at(sign.zones, point)) {
        return zone;
    }
    const terrain_mesh* ribbon = own_ribbon(area, sign);
    if (ribbon == nullptr) {
        return std::nullopt;
    }
    const std::optional<terrain_sample> on_ribbon = sample_terrain_inside(*ribbon, point);
    if (!on_ribbon) {
        return std::nullopt;
    }
    return on_ribbon->material;
}

hole_sign_labels make_hole_sign_labels(const text_assets& text, const int hole_number, const int par, const int meters) {
    return hole_sign_labels{
        format_text(text, text_hole_sign_number, {{"hole", std::to_string(hole_number)}}),
        format_text(text, text_hole_sign_par, {{"par", std::to_string(par)}}),
        format_text(text, text_hole_sign_length, {{"meters", std::to_string(meters)}}),
    };
}

ui_rect hole_sign_texels(const int left, const int top, const int width, const int height) {
    const float texel_x = 2.0f / static_cast<float>(hole_sign_face_width);
    const float texel_y = 2.0f / static_cast<float>(hole_sign_face_height);
    return rect_from_edges(-1.0f + static_cast<float>(left) * texel_x,
                           1.0f - static_cast<float>(top + height) * texel_y,
                           -1.0f + static_cast<float>(left + width) * texel_x,
                           1.0f - static_cast<float>(top) * texel_y);
}

void draw_hole_sign_face(overlay_batch& batch,
                         const text_assets& text,
                         const hole_sign_labels& labels,
                         const play_area& area,
                         const hole_sign& sign) {
    batch.grid = overlay_grid{hole_sign_face_width, hole_sign_face_height};
    const ui_rect panel = hole_sign_texels(frame_texels, frame_texels, hole_sign_face_width - frame_texels * 2,
                                           hole_sign_face_height - frame_texels * 2);
    draw_overlay_quad(batch, panel.center, panel.half_size, panel_paint);

    const ui_rect paper = hole_sign_texels(hole_sign_map_left - 1, hole_sign_map_top - 1, hole_sign_map_width + 2,
                                           hole_sign_map_height + 2);
    draw_overlay_quad(batch, paper.center, paper.half_size, course_map_paper);
    const ui_rect map = hole_sign_texels(hole_sign_map_left, hole_sign_map_top, hole_sign_map_width, hole_sign_map_height);
    const hole_sign_map_frame frame = make_hole_sign_map_frame(area, sign);
    draw_hole_map_ground(batch, area, sign, frame, map);
    draw_hole_map_marks(batch, sign, frame, map);

    const int label_width = hole_sign_face_width - frame_texels * 2;
    const int detail_width = label_width / 2;
    draw_label(batch, text.font, find_text_style(text, style_hole_sign_number), labels.number,
               hole_sign_texels(frame_texels, number_top, label_width, number_height));
    draw_label(batch, text.font, find_text_style(text, style_hole_sign_detail), labels.par,
               hole_sign_texels(frame_texels, detail_top, detail_width, detail_height));
    draw_label(batch, text.font, find_text_style(text, style_hole_sign_detail), labels.length,
               hole_sign_texels(frame_texels + detail_width, detail_top, label_width - detail_width, detail_height));
}

