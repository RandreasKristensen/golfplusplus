#include "game/floodlights.h"

#include "game/hole_sign.h"
#include "game/play_area.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

#include <glm/geometric.hpp>

namespace {
// How far along the line of play either side of a point its direction is
// taken over, and how finely a green's edge is found.
constexpr float direction_span = 2.0f;
constexpr float green_edge_step = 0.5f;
constexpr float green_edge_limit = 60.0f;
// How finely a lamp's beam is followed through the trees' canopies.
constexpr float beam_step = 0.5f;

glm::vec3 direction_at(const std::vector<glm::vec3>& line, const float distance) {
    return safe_normalize(horizontal(point_along(line, distance + direction_span) -
                                     point_along(line, distance - direction_span)),
                          glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::vec3 right_of(const glm::vec3& forward) {
    return -yaw_left(forward);
}

float distance_to_segment(const glm::vec3& point, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 p = horizontal(point);
    const glm::vec3 start = horizontal(a);
    const glm::vec3 ab = horizontal(b) - start;
    const float length_squared = glm::dot(ab, ab);
    const float t = length_squared <= 0.0001f ? 0.0f : clamp01(glm::dot(p - start, ab) / length_squared);
    return glm::length(p - (start + ab * t));
}

// A hole's line of play from the front of its tee box (`start` along
// `points`) to its pin.
struct play_line {
    std::vector<glm::vec3> points;
    float start = 0.0f;
};

std::vector<play_line> play_lines(const play_area& area, const std::vector<hole_data>& holes) {
    std::vector<play_line> lines;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        const tee_box* box = hole_tee_box(area.tee_boxes, i);
        lines.push_back(play_line{line_of_play(holes[i]), box ? box->half_length : 0.0f});
    }
    return lines;
}

// Within `half_width` of `line` past its start, and not past its pin.
bool in_play_line(const glm::vec3& spot, const play_line& line, const float half_width) {
    const glm::vec3 p = horizontal(spot);
    float walked = 0.0f;
    for (std::size_t i = 1; i < line.points.size(); ++i) {
        const glm::vec3 start = horizontal(line.points[i - 1]);
        const glm::vec3 ab = horizontal(line.points[i]) - start;
        const float length = glm::length(ab);
        if (length <= 0.0001f) {
            continue;
        }
        const float along = glm::dot(p - start, ab) / length;
        const float clamped = std::clamp(along, 0.0f, length);
        const bool past_pin = i + 1 == line.points.size() && along > length;
        if (!past_pin && walked + clamped > line.start && glm::length(p - (start + ab * (clamped / length))) < half_width) {
            return true;
        }
        walked += length;
    }
    return false;
}

// A tree's canopy: a cone up from `bottom`, `radius` across at its foot.
struct canopy {
    glm::vec3 bottom{0.0f};
    float radius = 0.0f;
    float height = 0.0f;
};

std::vector<canopy> canopies(const play_area& area) {
    std::vector<canopy> out;
    for (const tree_instance& tree : area.trees) {
        if (tree.shape.leaf_height > 0.0f) {
            out.push_back(canopy{anchor_on_terrain(area, tree.position) + world_up * std::max(0.0f, tree.shape.trunk_height),
                                 tree.shape.leaf_radius, tree.shape.leaf_height});
        }
    }
    return out;
}

// True when the straight beam from `lamp` to `lit` passes through no canopy.
bool beam_clear(const glm::vec3& lamp, const glm::vec3& lit, const std::vector<canopy>& trees) {
    const int steps = std::max(1, static_cast<int>(std::ceil(glm::length(lit - lamp) / beam_step)));
    for (const canopy& tree : trees) {
        if (distance_to_segment(tree.bottom, lamp, lit) >= tree.radius) {
            continue;
        }
        for (int i = 0; i <= steps; ++i) {
            const glm::vec3 point = lamp + (lit - lamp) * (static_cast<float>(i) / static_cast<float>(steps));
            const float up = point.y - tree.bottom.y;
            if (up >= 0.0f && up <= tree.height &&
                horizontal_distance(point, tree.bottom) < tree.radius * (1.0f - up / tree.height)) {
                return false;
            }
        }
    }
    return true;
}

struct placement {
    const play_area& area;
    const std::vector<play_line>& lines;
    const std::vector<canopy>& trees;
    const std::vector<course_world_cart_road>& roads;
    const game_tuning& tuning;
    const std::vector<floodlight>& placed;
};

// How far the pole and its lamp head reach out from the pole's axis.
float pole_reach(const floodlight_tuning& rules) {
    return std::max(rules.pole_radius, rules.arm_length + std::max(rules.head_width, rules.head_depth) * 0.5f);
}

// Fairway, green, sand, water or a tee box.
bool playing_surface(const glm::vec3& spot, const placement& at) {
    const play_area& area = at.area;
    return sample_area(area, spot).material != terrain_material::rough ||
        std::any_of(area.tee_boxes.begin(), area.tee_boxes.end(), [&](const tee_box& box) { return on_tee_box(box, spot); });
}

bool clear_of(const glm::vec3& spot, const glm::vec3& other, const float other_radius, const placement& at) {
    const floodlight_tuning& rules = at.tuning.floodlights;
    return horizontal_distance(spot, other) >= other_radius + rules.pole_radius + rules.obstacle_clearance;
}

// Rough, obstacle_clearance off every playing surface, off every cart road,
// out of every hole's line of play, its lamp head clear of every canopy,
// and clear of every post, net and pole already standing.
bool open_ground(const glm::vec3& spot, const placement& at) {
    const play_area& area = at.area;
    const floodlight_tuning& rules = at.tuning.floodlights;
    const float margin = rules.pole_radius + rules.obstacle_clearance;
    if (playing_surface(spot, at) || on_cart_road(at.roads, spot, at.tuning.cart)) {
        return false;
    }
    for (const glm::vec3& out : {glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                                 glm::vec3(0.0f, 0.0f, -1.0f)}) {
        if (playing_surface(spot + out * margin, at)) {
            return false;
        }
    }
    if (std::any_of(at.lines.begin(), at.lines.end(),
                    [&](const play_line& line) { return in_play_line(spot, line, rules.play_line_half_width); })) {
        return false;
    }
    const bool clear_of_trees = std::all_of(area.trees.begin(), area.trees.end(), [&](const tree_instance& tree) {
        return horizontal_distance(spot, tree.position) >=
            std::max(tree.shape.trunk_radius, tree.shape.leaf_radius) + pole_reach(rules) + rules.obstacle_clearance;
    });
    const bool clear_of_poles = std::all_of(at.placed.begin(), at.placed.end(), [&](const floodlight& light) {
        return clear_of(spot, light.foot, light.radius, at);
    });
    const bool clear_of_signs = std::all_of(area.signs.begin(), area.signs.end(), [&](const hole_sign& sign) {
        return std::all_of(sign.post_feet.begin(), sign.post_feet.end(),
                           [&](const glm::vec3& foot) { return clear_of(spot, foot, sign.post_radius, at); });
    });
    const bool clear_of_fences = std::all_of(area.fences.begin(), area.fences.end(), [&](const course_world_fence& fence) {
        for (std::size_t i = 0; i < fence.poles.size(); ++i) {
            if (!clear_of(spot, fence.poles[i], at.tuning.fence.pole_radius, at) ||
                (i + 1 < fence.poles.size() && distance_to_segment(spot, fence.poles[i], fence.poles[i + 1]) < margin)) {
                return false;
            }
        }
        return true;
    });
    return clear_of_trees && clear_of_poles && clear_of_signs && clear_of_fences;
}

// A pole at `spot`, its lamp aimed at `target`.
floodlight pole_at(const glm::vec3& spot, const glm::vec3& target, const std::size_t area_hole,
                   const floodlight_target kind, const placement& at) {
    const floodlight_tuning& rules = at.tuning.floodlights;
    floodlight light;
    light.area_hole = area_hole;
    light.target = kind;
    light.foot = anchor_on_terrain(at.area, spot);
    light.radius = rules.pole_radius;
    const glm::vec3 lit = anchor_on_terrain(at.area, target);
    const glm::vec3 towards = safe_normalize(horizontal(lit - light.foot), glm::vec3(0.0f, 0.0f, 1.0f));
    light.lamp = light.foot + world_up * rules.pole_height + towards * rules.arm_length;
    light.aim = safe_normalize(lit - light.lamp, -world_up);
    return light;
}

// A pole at the first open spot from `spot` moved `outward` steps of
// nudge_step out and `along` steps either way, the nearest tried first,
// whose beam reaches `target` through no canopy.
std::optional<floodlight> pole_near(const glm::vec3& spot, const glm::vec3& outward, const glm::vec3& along,
                                    const glm::vec3& target, const std::size_t area_hole, const floodlight_target kind,
                                    const placement& at) {
    const floodlight_tuning& rules = at.tuning.floodlights;
    for (int ring = 0; ring <= std::max(0, rules.nudge_tries); ++ring) {
        for (int out = 0; out <= ring; ++out) {
            for (int side = -ring; side <= ring; ++side) {
                if (std::max(out, std::abs(side)) != ring) {
                    continue;
                }
                const glm::vec3 candidate = spot + (outward * static_cast<float>(out) + along * static_cast<float>(side)) * rules.nudge_step;
                if (!open_ground(candidate, at)) {
                    continue;
                }
                const floodlight light = pole_at(candidate, target, area_hole, kind, at);
                if (beam_clear(light.lamp, anchor_on_terrain(at.area, target), at.trees)) {
                    return light;
                }
            }
        }
    }
    return std::nullopt;
}

// Where to look for a pole: from `spot`, nudged `outward` and `along`.
struct pole_try {
    glm::vec3 spot{0.0f};
    glm::vec3 outward{0.0f};
    glm::vec3 along{0.0f};
};

// The pole near the first of `tries` that has an open spot.
std::optional<floodlight> first_pole(const std::vector<pole_try>& tries, const glm::vec3& target, const std::size_t area_hole,
                                     const floodlight_target kind, const placement& at) {
    for (const pole_try& where : tries) {
        if (std::optional<floodlight> light = pole_near(where.spot, where.outward, where.along, target, area_hole, kind, at)) {
            return light;
        }
    }
    return std::nullopt;
}

// How far from the pin the green ends towards `outward`.
float green_edge(const play_area& area, const glm::vec3& pin, const glm::vec3& outward) {
    float edge = 0.0f;
    while (edge < green_edge_limit && sample_area(area, pin + outward * edge).material == terrain_material::green) {
        edge += green_edge_step;
    }
    return edge;
}

void place_hole_floodlights(std::vector<floodlight>& lights, const play_area& area, const hole_data& hole,
                            const std::size_t area_hole, const std::vector<play_line>& lines, const std::vector<canopy>& trees,
                            const std::vector<course_world_cart_road>& roads, const game_tuning& tuning) {
    const floodlight_tuning& rules = tuning.floodlights;
    const placement at{area, lines, trees, roads, tuning, lights};
    const auto add = [&lights](const std::optional<floodlight>& light) {
        if (light) {
            lights.push_back(*light);
        }
    };

    // The tee's, out past the tee box's left edge (its sign stands on the
    // right), else its right, else behind it.
    if (const tee_box* tee_box_here = hole_tee_box(area.tee_boxes, area_hole)) {
        const tee_box& box = *tee_box_here;
        const glm::vec3 left = -right_of(box.down_hole);
        const float across = box.half_width + rules.tee_offset;
        add(first_pole({{box.center + left * across, left, box.down_hole},
                        {box.center - left * across, -left, box.down_hole},
                        {box.center - box.down_hole * (box.half_length + rules.tee_offset), -box.down_hole, left}},
                       box.center, area_hole, floodlight_target::tee, at));
    }

    // The fairway's, spacing apart from tee_clearance to green_clearance
    // short of the pin, centred in that stretch: right, left, right...
    const std::vector<glm::vec3> line = line_of_play(hole);
    const float length = polyline_length(line);
    const float stretch = length - rules.tee_clearance - rules.green_clearance;
    if (stretch >= 0.0f && rules.spacing > 0.0f) {
        const int count = 1 + static_cast<int>(std::floor(stretch / rules.spacing));
        const float start = rules.tee_clearance + (stretch - static_cast<float>(count - 1) * rules.spacing) * 0.5f;
        const float fairway_half = hole.spline.width * 0.5f;
        for (int i = 0; i < count; ++i) {
            const float distance = start + static_cast<float>(i) * rules.spacing;
            const glm::vec3 centre = point_along(line, distance);
            const glm::vec3 forward = direction_at(line, distance);
            const glm::vec3 outward = right_of(forward) * (i % 2 == 0 ? 1.0f : -1.0f);
            add(pole_near(centre + outward * (fairway_half + rules.fairway_offset), outward, forward, centre, area_hole,
                          floodlight_target::fairway, at));
        }
    }

    // The green's, one each side of the pin out past the green's edge, else
    // past its back.
    const glm::vec3 pin = hole.pin_position;
    const glm::vec3 forward = direction_at(line, length - direction_span);
    const glm::vec3 behind = pin + forward * (green_edge(area, pin, forward) + rules.green_offset);
    for (const float side : {1.0f, -1.0f}) {
        const glm::vec3 outward = right_of(forward) * side;
        add(first_pole({{pin + outward * (green_edge(area, pin, outward) + rules.green_offset), outward, forward},
                        {behind, forward, outward}},
                       pin, area_hole, floodlight_target::green, at));
    }
}
}

std::vector<floodlight> place_floodlights(const play_area& area,
                                          const std::vector<hole_data>& holes,
                                          const std::vector<course_world_cart_road>& roads,
                                          const game_tuning& tuning) {
    std::vector<floodlight> lights;
    const std::vector<play_line> lines = play_lines(area, holes);
    const std::vector<canopy> trees = canopies(area);
    for (std::size_t i = 0; i < holes.size(); ++i) {
        place_hole_floodlights(lights, area, holes[i], i, lines, trees, roads, tuning);
    }
    return lights;
}

