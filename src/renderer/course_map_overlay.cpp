#include "renderer/course_map_overlay.h"

#include "game/hole_sign.h"
#include "game/text_ids.h"
#include "renderer/pixel_font.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace {
const glm::vec3 map_paper_shadow(0.14f, 0.12f, 0.09f);
const glm::vec3 map_fold(0.42f, 0.35f, 0.24f);
const glm::vec3 map_marker_outline(0.04f, 0.035f, 0.025f);
const glm::vec3 map_ball_ink(0.94f, 0.93f, 0.82f);
const glm::vec3 map_player_ink(0.22f, 0.46f, 0.72f);
const glm::vec3 map_cup_ink(0.94f, 0.78f, 0.22f);
const glm::vec3 map_bag_ink(0.62f, 0.2f, 0.14f);

// Clip units between the paper's edge and the course on it.
constexpr float map_margin = 0.06f;
// The marks' sizes, in clip units.
constexpr float player_mark_radius = 0.024f;
constexpr float tee_mark_radius = 0.023f;
constexpr float ball_mark_radius = 0.020f;
constexpr float cup_mark_radius = 0.018f;
constexpr float bag_mark_radius = 0.018f;
const glm::vec2 flagstick_offset(0.0f, 0.028f);
const glm::vec2 flagstick_half(0.004f, 0.042f);
const glm::vec2 flag_offset(0.022f, 0.055f);
const glm::vec2 flag_half(0.028f, 0.018f);
// A tee box is drawn at least this big (half its longer side, clip units),
// so a small one still reads; a hole's line of play this thick and faint.
constexpr float least_tee_half_size = 0.014f;
constexpr float tee_outline = 0.004f;
constexpr float line_of_play_thickness = 0.004f;
constexpr float line_of_play_alpha = 0.45f;
// A hole number's box is twice as wide as tall, and as tall as the first of
// these shares of the paper's height at which no two numbers overlap.
constexpr std::array<float, 4> number_box_shares{1.0f / 14.0f, 1.0f / 18.0f, 1.0f / 22.0f, 1.0f / 26.0f};
constexpr float number_box_aspect = 2.0f;
constexpr float number_backing_alpha = 0.85f;
// Where along its hole's line of play a number may sit, as shares of its
// length: the middle, then further out to either side while that is taken.
constexpr std::array<float, 13> number_places{0.5f,  0.46f, 0.54f, 0.42f, 0.58f, 0.38f, 0.62f,
                                              0.34f, 0.66f, 0.3f,  0.7f,  0.26f, 0.74f};
// Space wanted between a number's box and the other numbers and marks, as a
// share of the box's half size.
constexpr float number_spacing = 0.25f;
// How many times overlapping numbers are all placed again before a smaller box is tried.
constexpr int number_replacing_rounds = 4;

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

// The tee box's footprint on the map, grown about its middle to the least
// size; `grow` more on every side (its outline).
std::array<glm::vec2, 4> tee_corners(const course_map_layout& layout, const tee_box& box, const float grow) {
    const glm::vec3 across(-box.down_hole.z, 0.0f, box.down_hole.x);
    const glm::vec2 middle = map_point(layout, box.center);
    std::array<glm::vec2, 4> corners{};
    const std::array<glm::vec2, 4> signs{glm::vec2(-1.0f, -1.0f), glm::vec2(1.0f, -1.0f), glm::vec2(1.0f, 1.0f),
                                         glm::vec2(-1.0f, 1.0f)};
    float reach = 0.0f;
    for (std::size_t i = 0; i < corners.size(); ++i) {
        corners[i] = map_point(layout, box.center + across * (signs[i].x * box.half_width) +
                                           box.down_hole * (signs[i].y * box.half_length)) - middle;
        reach = std::max(reach, std::max(std::abs(corners[i].x), std::abs(corners[i].y)));
    }
    const float scale = reach > 0.0f ? std::max(1.0f, least_tee_half_size / reach) : 1.0f;
    for (glm::vec2& corner : corners) {
        const float length = glm::length(corner);
        corner = middle + corner * scale + (length > 0.0f ? corner / length * grow : glm::vec2(0.0f));
    }
    return corners;
}

void draw_tee_box(overlay_batch& batch, const course_map_layout& layout, const tee_box& box) {
    const std::array<glm::vec2, 4> outline = tee_corners(layout, box, tee_outline);
    draw_overlay_corner_quad(batch, outline[0], outline[1], outline[2], outline[3], map_marker_outline, 0.75f);
    const std::array<glm::vec2, 4> fill = tee_corners(layout, box, 0.0f);
    draw_overlay_corner_quad(batch, fill[0], fill[1], fill[2], fill[3], course_map_tee_ink, 1.0f);
}

void draw_hole_number(overlay_batch& batch, const text_assets& text, const ui_rect& box, const render_map_hole& hole) {
    const text_style& style = find_text_style(text, style_course_map_hole_number);
    const text_layout number = layout_text(text.font, style, hole.number, box, batch.grid);
    draw_overlay_quad(batch, number.bounds.center, number.bounds.half_size + number.cell, course_map_paper, number_backing_alpha);
    draw_text_layout(batch, text.font, number, style.color);
}

// What the numbers keep clear of: my bag and, on a hole, its tee, ball, pin
// and flag. Not me: walking about never moves a number; I am drawn under them.
std::vector<ui_rect> marks_kept_clear(const course_map_layout& layout, const render_data& data) {
    std::vector<ui_rect> marks;
    if (data.map_bag_position) {
        marks.push_back({map_point(layout, *data.map_bag_position), glm::vec2(bag_mark_radius)});
    }
    if (data.show_hole) {
        const glm::vec2 pin = map_point(layout, data.pin_position);
        marks.push_back({map_point(layout, data.tee_position), glm::vec2(tee_mark_radius)});
        marks.push_back({map_point(layout, data.ball_position), glm::vec2(ball_mark_radius)});
        marks.push_back({pin, glm::vec2(cup_mark_radius)});
        marks.push_back({pin + flagstick_offset, flagstick_half});
        marks.push_back({pin + flag_offset, flag_half});
    }
    return marks;
}

struct number_placement {
    std::vector<ui_rect> boxes;
    bool overlapping = false;  // two numbers' boxes overlap
};

// Each hole's number in a box of `half` size at the first place along its
// line of play that clears what is placed before it and the paper's edge,
// else the clearest. While two overlap, each is placed again against all the
// others, so a number placed early can make room for a later neighbour's.
number_placement place_numbers(const course_map_layout& layout,
                               const render_data& data,
                               const std::vector<ui_rect>& marks,
                               const glm::vec2 half) {
    const glm::vec2 low = layout.center - layout.half_size + half;
    const glm::vec2 high = layout.center + layout.half_size - half;
    number_placement placement;
    placement.boxes.reserve(data.map_holes.size());
    // How far `box` is from the nearest of `others` but `skipped`, in box half sizes.
    const auto clearance = [&](const ui_rect& box, const std::vector<ui_rect>& others, const std::size_t skipped) {
        float nearest = std::numeric_limits<float>::max();
        for (std::size_t i = 0; i < others.size(); ++i) {
            if (i == skipped) {
                continue;
            }
            const glm::vec2 gap = (glm::abs(box.center - others[i].center) - (box.half_size + others[i].half_size)) / half;
            nearest = std::min(nearest, std::max(gap.x, gap.y));
        }
        return nearest;
    };
    const auto place = [&](const render_map_hole& hole, const std::size_t own) {
        const float length = polyline_length(hole.line_of_play);
        ui_rect chosen;
        float chosen_clearance = std::numeric_limits<float>::lowest();
        for (const float share : number_places) {
            const glm::vec2 point = map_point(layout, point_along(hole.line_of_play, length * share));
            const ui_rect box{glm::clamp(point, low, high), half};
            // Pushed in off its hole by the paper's edge: only if no place is clear.
            const float edge = box.center == point ? std::numeric_limits<float>::max() : 0.0f;
            const float box_clearance =
                std::min({clearance(box, placement.boxes, own), clearance(box, marks, marks.size()), edge});
            if (box_clearance > chosen_clearance) {
                chosen = box;
                chosen_clearance = box_clearance;
            }
            if (chosen_clearance >= number_spacing) {
                break;
            }
        }
        return chosen;
    };
    const auto any_overlap = [&] {
        for (std::size_t i = 0; i < placement.boxes.size(); ++i) {
            if (clearance(placement.boxes[i], placement.boxes, i) < 0.0f) {
                return true;
            }
        }
        return false;
    };
    for (const render_map_hole& hole : data.map_holes) {
        placement.boxes.push_back(place(hole, placement.boxes.size()));
    }
    placement.overlapping = any_overlap();
    for (int round = 0; round < number_replacing_rounds && placement.overlapping; ++round) {
        for (std::size_t i = 0; i < data.map_holes.size(); ++i) {
            placement.boxes[i] = place(data.map_holes[i], i);
        }
        placement.overlapping = any_overlap();
    }
    return placement;
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
    if (data.map_bag_position) {
        cover(*data.map_bag_position, low, high);
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
                                             const render_data& data) {
    const glm::vec2 per_clip = pixels_per_clip(grid);
    const std::vector<ui_rect> marks = marks_kept_clear(layout, data);
    number_placement placement;
    for (const float share : number_box_shares) {
        const float height = layout.half_size.y * 2.0f * share;
        const glm::vec2 half(height * number_box_aspect * per_clip.y / per_clip.x * 0.5f, height * 0.5f);
        placement = place_numbers(layout, data, marks, half);
        if (!placement.overlapping) {
            break;
        }
    }
    return placement.boxes;
}

void draw_course_map_marks(overlay_batch& batch,
                           const text_assets& text,
                           const course_map_layout& layout,
                           const render_data& data) {
    for (const render_map_hole& hole : data.map_holes) {
        for (std::size_t i = 0; i + 1 < hole.line_of_play.size(); ++i) {
            draw_overlay_segment(batch, map_point(layout, hole.line_of_play[i]), map_point(layout, hole.line_of_play[i + 1]),
                                 line_of_play_thickness, course_map_tee_ink, line_of_play_alpha);
        }
    }
    if (data.trees != nullptr) {
        for (const tree_body& tree : *data.trees) {
            const float radius = std::clamp(tree.shape.leaf_radius * layout.scale.y, 0.012f, 0.028f);
            draw_marker(batch, map_point(layout, tree.base), course_map_tree_ink, radius);
        }
    }
    for (const render_map_hole& hole : data.map_holes) {
        if (hole.tee) {
            draw_tee_box(batch, layout, *hole.tee);
        }
    }
    if (data.show_hole) {
        draw_marker(batch, map_point(layout, data.tee_position), course_map_tee_ink, tee_mark_radius);
        draw_marker(batch, map_point(layout, data.ball_position), map_ball_ink, ball_mark_radius);
    }
    if (data.map_bag_position) {
        draw_marker(batch, map_point(layout, *data.map_bag_position), map_bag_ink, bag_mark_radius);
    }
    draw_marker(batch, map_point(layout, data.player_position), map_player_ink, player_mark_radius);
    if (data.show_hole) {
        const glm::vec2 pin = map_point(layout, data.pin_position);
        draw_overlay_quad(batch, pin + flagstick_offset, flagstick_half, course_map_flagstick_ink, 0.92f);
        draw_overlay_quad(batch, pin + flag_offset, flag_half, course_map_flag_ink, 0.96f);
        draw_marker(batch, pin, map_cup_ink, cup_mark_radius);
    }
    const std::vector<ui_rect> boxes = course_map_number_boxes(layout, batch.grid, data);
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        draw_hole_number(batch, text, boxes[i], data.map_holes[i]);
    }
}
