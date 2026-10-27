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

struct placement {
    const play_area& area;
    const std::vector<course_world_cart_road>& roads;
    const game_tuning& tuning;
    const std::vector<floodlight>& placed;
};

bool clear_of(const glm::vec3& spot, const glm::vec3& other, const float other_radius, const placement& at) {
    const floodlight_tuning& rules = at.tuning.floodlights;
    return horizontal_distance(spot, other) >= other_radius + rules.pole_radius + rules.obstacle_clearance;
}

// Rough, off every tee box and cart road, and clear of every trunk, post,
// net and pole already standing.
bool open_ground(const glm::vec3& spot, const placement& at) {
    const play_area& area = at.area;
    if (sample_area(area, spot).material != terrain_material::rough || on_cart_road(at.roads, spot, at.tuning.cart)) {
        return false;
    }
    for (const tee_box& box : area.tee_boxes) {
        if (on_tee_box(box, spot)) {
            return false;
        }
    }
    const bool clear_of_trees = std::all_of(area.trees.begin(), area.trees.end(), [&](const tree_instance& tree) {
        return clear_of(spot, tree.position, tree.shape.trunk_radius, at);
    });
    const bool clear_of_poles = std::all_of(at.placed.begin(), at.placed.end(), [&](const floodlight& light) {
        return clear_of(spot, light.foot, light.radius, at);
    });
    const bool clear_of_signs = std::all_of(area.signs.begin(), area.signs.end(), [&](const hole_sign& sign) {
        return std::all_of(sign.post_feet.begin(), sign.post_feet.end(),
                           [&](const glm::vec3& foot) { return clear_of(spot, foot, sign.post_radius, at); });
    });
    const float net_clearance = at.tuning.floodlights.pole_radius + at.tuning.floodlights.obstacle_clearance;
    const bool clear_of_fences = std::all_of(area.fences.begin(), area.fences.end(), [&](const course_world_fence& fence) {
        for (std::size_t i = 0; i < fence.poles.size(); ++i) {
            if (!clear_of(spot, fence.poles[i], at.tuning.fence.pole_radius, at) ||
                (i + 1 < fence.poles.size() && distance_to_segment(spot, fence.poles[i], fence.poles[i + 1]) < net_clearance)) {
                return false;
            }
        }
        return true;
    });
    return clear_of_trees && clear_of_poles && clear_of_signs && clear_of_fences;
}

// The first open spot from `spot` moved `outward` steps of nudge_step out
// and `along` steps either way, the nearest tried first.
std::optional<glm::vec3> nearest_open_spot(const glm::vec3& spot, const glm::vec3& outward, const glm::vec3& along,
                                           const placement& at) {
    const floodlight_tuning& rules = at.tuning.floodlights;
    for (int ring = 0; ring <= std::max(0, rules.nudge_tries); ++ring) {
        for (int out = 0; out <= ring; ++out) {
            for (int side = -ring; side <= ring; ++side) {
                if (std::max(out, std::abs(side)) != ring) {
                    continue;
                }
                const glm::vec3 candidate = spot + (outward * static_cast<float>(out) + along * static_cast<float>(side)) * rules.nudge_step;
                if (open_ground(candidate, at)) {
                    return candidate;
                }
            }
        }
    }
    return std::nullopt;
}

// A pole at the first open spot near `spot`, its lamp aimed at `target`.
std::optional<floodlight> pole_near(const glm::vec3& spot, const glm::vec3& outward, const glm::vec3& along,
                                    const glm::vec3& target, const std::size_t area_hole, const floodlight_target kind,
                                    const placement& at) {
    const std::optional<glm::vec3> open = nearest_open_spot(spot, outward, along, at);
    if (!open) {
        return std::nullopt;
    }
    const floodlight_tuning& rules = at.tuning.floodlights;
    floodlight light;
    light.area_hole = area_hole;
    light.target = kind;
    light.foot = anchor_on_terrain(at.area, *open);
    light.radius = rules.pole_radius;
    const glm::vec3 lit = anchor_on_terrain(at.area, target);
    const glm::vec3 towards = safe_normalize(horizontal(lit - light.foot), glm::vec3(0.0f, 0.0f, 1.0f));
    light.lamp = light.foot + world_up * rules.pole_height + towards * rules.arm_length;
    light.aim = safe_normalize(lit - light.lamp, -world_up);
    return light;
}

void place_hole_floodlights(std::vector<floodlight>& lights, const play_area& area, const hole_data& hole,
                            const std::size_t area_hole, const std::vector<course_world_cart_road>& roads,
                            const game_tuning& tuning) {
    const floodlight_tuning& rules = tuning.floodlights;
    const placement at{area, roads, tuning, lights};
    const auto add = [&lights](const std::optional<floodlight>& light) {
        if (light) {
            lights.push_back(*light);
        }
    };

    // The tee's, out past the tee box's left edge (its sign stands on the right).
    if (area_hole < area.tee_boxes.size()) {
        const tee_box& box = area.tee_boxes[area_hole];
        const glm::vec3 left = -right_of(box.down_hole);
        add(pole_near(box.center + left * (box.half_width + rules.tee_offset), left, box.down_hole, box.center, area_hole,
                      floodlight_target::tee, at));
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

    // The green's, one each side of the pin out past the green's edge.
    const glm::vec3 pin = hole.pin_position;
    const glm::vec3 forward = direction_at(line, length - direction_span);
    for (const float side : {1.0f, -1.0f}) {
        const glm::vec3 outward = right_of(forward) * side;
        float edge = 0.0f;
        while (edge < green_edge_limit && sample_area(area, pin + outward * edge).material == terrain_material::green) {
            edge += green_edge_step;
        }
        add(pole_near(pin + outward * (edge + rules.green_offset), outward, forward, pin, area_hole,
                      floodlight_target::green, at));
    }
}
}

std::vector<floodlight> place_floodlights(const play_area& area,
                                          const std::vector<hole_data>& holes,
                                          const std::vector<course_world_cart_road>& roads,
                                          const game_tuning& tuning) {
    std::vector<floodlight> lights;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        place_hole_floodlights(lights, area, holes[i], i, roads, tuning);
    }
    return lights;
}

