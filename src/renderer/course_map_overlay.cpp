#include "renderer/course_map_overlay.h"

#include "game/text_ids.h"
#include "renderer/pixel_font.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

#include <glm/common.hpp>
#include <glm/vec2.hpp>

namespace {
const glm::vec3 map_paper_shadow(0.14f, 0.12f, 0.09f);
const glm::vec3 map_fold(0.42f, 0.35f, 0.24f);
const glm::vec3 map_marker_outline(0.04f, 0.035f, 0.025f);
const glm::vec3 map_ball_ink(0.94f, 0.93f, 0.82f);
const glm::vec3 map_player_ink(0.22f, 0.46f, 0.72f);
const glm::vec3 map_cup_ink(0.94f, 0.78f, 0.22f);

// Clip units between the paper's edge and the course on it.
constexpr float map_margin = 0.06f;
// A hole number's box: this share of the paper's height, twice as wide as tall.
constexpr float number_box_share = 1.0f / 14.0f;
constexpr float number_box_aspect = 2.0f;
constexpr float number_backing_alpha = 0.85f;
// Space kept between two numbers' boxes, as a share of a box's half size,
// trying this many rings of places around the tee before taking the
// clearest one.
constexpr float number_spacing = 0.25f;
constexpr int number_rings = 3;

// Target pixels per clip unit (a square grid when there is none).
glm::vec2 pixels_per_clip(const overlay_grid grid) {
    if (grid.width <= 0 || grid.height <= 0) {
        return glm::vec2(1.0f);
    }
    return glm::vec2(static_cast<float>(grid.width), static_cast<float>(grid.height)) * 0.5f;
}

void cover(const glm::vec3& position, glm::vec2& low, glm::vec2& high) {
    low = glm::min(low, glm::vec2(position.x, position.z));
    high = glm::max(high, glm::vec2(position.x, position.z));
}

void draw_marker(overlay_batch& batch, const glm::vec2 position, const glm::vec3 color, const float radius) {
    draw_overlay_quad(batch, position, glm::vec2(radius), map_marker_outline, 0.55f);
    draw_overlay_quad(batch, position, glm::vec2(radius * 0.64f), color, 1.0f);
}

void draw_hole_number(overlay_batch& batch, const text_assets& text, const ui_rect& box, const render_map_hole& hole) {
    const text_style& style = find_text_style(text, style_course_map_hole_number);
    const text_layout number = layout_text(text.font, style, hole.number, box, batch.grid);
    draw_overlay_quad(batch, number.bounds.center, number.bounds.half_size + number.cell, course_map_paper, number_backing_alpha);
    draw_text_layout(batch, text.font, number, style.color);
}
}

course_map_layout make_course_map_layout(const render_data& data, const overlay_grid grid) {
    glm::vec2 low(data.player_position.x, data.player_position.z);
    glm::vec2 high = low;
    if (data.show_hole) {
        cover(data.tee_position, low, high);
        cover(data.pin_position, low, high);
        cover(data.ball_position, low, high);
    }
    if (data.course_map_low.x < data.course_map_high.x && data.course_map_low.z < data.course_map_high.z) {
        cover(data.course_map_low, low, high);
        cover(data.course_map_high, low, high);
    }

    course_map_layout layout;
    const glm::vec2 span = glm::max(high - low, glm::vec2(1.0f));
    const glm::vec2 per_clip = pixels_per_clip(grid);
    const glm::vec2 drawable = (layout.half_size - glm::vec2(map_margin)) * 2.0f * per_clip;
    const float pixels_per_unit = std::min(drawable.x / span.x, drawable.y / span.y);
    layout.world_center = glm::vec3((low.x + high.x) * 0.5f, 0.0f, (low.y + high.y) * 0.5f);
    layout.scale = glm::vec2(pixels_per_unit) / per_clip;
    return layout;
}

void draw_course_map_paper(overlay_batch& batch, const course_map_layout& layout) {
    draw_overlay_quad(batch, layout.center + glm::vec2(0.035f, -0.035f), layout.half_size, map_paper_shadow, 0.42f);
    draw_overlay_quad(batch, layout.center, layout.half_size, course_map_paper, 0.96f);
    draw_overlay_quad(batch, layout.center, glm::vec2(layout.half_size.x, 0.006f), map_fold, 0.18f);
    draw_overlay_quad(batch, layout.center, glm::vec2(0.006f, layout.half_size.y), map_fold, 0.18f);
    for (const float side : {1.0f, -1.0f}) {
        draw_overlay_quad(batch, layout.center + glm::vec2(0.0f, side * layout.half_size.y),
                          glm::vec2(layout.half_size.x, 0.012f), map_fold, 0.46f);
        draw_overlay_quad(batch, layout.center + glm::vec2(side * layout.half_size.x, 0.0f),
                          glm::vec2(0.012f, layout.half_size.y), map_fold, 0.46f);
    }
}

std::vector<ui_rect> course_map_number_boxes(const course_map_layout& layout,
                                             const overlay_grid grid,
                                             const std::vector<render_map_hole>& holes) {
    const glm::vec2 per_clip = pixels_per_clip(grid);
    const float height = layout.half_size.y * 2.0f * number_box_share;
    const glm::vec2 half(height * number_box_aspect * per_clip.y / per_clip.x * 0.5f, height * 0.5f);
    const glm::vec2 low = layout.center - layout.half_size + half;
    const glm::vec2 high = layout.center + layout.half_size - half;
    // Beside the tee, so its mark stays visible: above, below, right, left,
    // then the diagonals, then the same each a half box further out.
    const std::array<glm::vec2, 8> directions{glm::vec2(0.0f, 1.0f),  glm::vec2(0.0f, -1.0f), glm::vec2(1.0f, 0.0f),
                                              glm::vec2(-1.0f, 0.0f), glm::vec2(1.0f, 1.0f),  glm::vec2(-1.0f, 1.0f),
                                              glm::vec2(1.0f, -1.0f), glm::vec2(-1.0f, -1.0f)};
    std::vector<ui_rect> boxes;
    boxes.reserve(holes.size());
    for (const render_map_hole& hole : holes) {
        const glm::vec2 tee = map_point(layout, hole.tee);
        // How far `box` is from the nearest number already placed, in box half sizes.
        const auto clearance = [&](const ui_rect& box) {
            float nearest = std::numeric_limits<float>::max();
            for (const ui_rect& placed : boxes) {
                const glm::vec2 gap = (glm::abs(box.center - placed.center) - (box.half_size + placed.half_size)) / half;
                nearest = std::min(nearest, std::max(gap.x, gap.y));
            }
            return nearest;
        };
        ui_rect chosen{glm::clamp(tee + glm::vec2(0.0f, half.y), low, high), half};
        float chosen_clearance = clearance(chosen);
        for (int ring = 1; ring <= number_rings && chosen_clearance < number_spacing; ++ring) {
            for (const glm::vec2 direction : directions) {
                const ui_rect box{glm::clamp(tee + direction * half * static_cast<float>(ring), low, high), half};
                const float box_clearance = clearance(box);
                if (box_clearance > chosen_clearance) {
                    chosen = box;
                    chosen_clearance = box_clearance;
                }
                if (chosen_clearance >= number_spacing) {
                    break;
                }
            }
        }
        boxes.push_back(chosen);
    }
    return boxes;
}

void draw_course_map_marks(overlay_batch& batch,
                           const text_assets& text,
                           const course_map_layout& layout,
                           const render_data& data) {
    if (data.trees != nullptr) {
        for (const tree_body& tree : *data.trees) {
            const float radius = std::clamp(tree.shape.leaf_radius * layout.scale.y, 0.012f, 0.028f);
            draw_marker(batch, map_point(layout, tree.base), course_map_tree_ink, radius);
        }
    }
    if (data.show_hole) {
        draw_marker(batch, map_point(layout, data.tee_position), course_map_tee_ink, 0.023f);
        draw_marker(batch, map_point(layout, data.ball_position), map_ball_ink, 0.020f);
    }
    draw_marker(batch, map_point(layout, data.player_position), map_player_ink, 0.024f);
    if (data.show_hole) {
        const glm::vec2 pin = map_point(layout, data.pin_position);
        draw_overlay_quad(batch, pin + glm::vec2(0.0f, 0.028f), glm::vec2(0.004f, 0.042f), course_map_flagstick_ink, 0.92f);
        draw_overlay_quad(batch, pin + glm::vec2(0.022f, 0.055f), glm::vec2(0.028f, 0.018f), course_map_flag_ink, 0.96f);
        draw_marker(batch, pin, map_cup_ink, 0.018f);
    }
    const std::vector<ui_rect> boxes = course_map_number_boxes(layout, batch.grid, data.map_holes);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        draw_hole_number(batch, text, boxes[i], data.map_holes[i]);
    }
}
