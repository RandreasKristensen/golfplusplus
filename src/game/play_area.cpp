#include "game/play_area.h"

#include "physics/ground_mesh.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
terrain_mesh place_mesh(const terrain_mesh& mesh, const hole_data& hole, const course_world_hole_start& start) {
    terrain_mesh placed = mesh;
    const float radians = glm::radians(start.rotation_degrees);
    for (terrain_vertex& vertex : placed.vertices) {
        vertex.position = place_hole_point(hole, start, vertex.position);
        vertex.normal = rotate_about_y(vertex.normal, radians);
    }
    return build_terrain_mesh_index(std::move(placed));
}

// The zone where the placed hole puts it: it keeps its size and turns with
// the hole around its own (placed) centre.
material_zone place_zone(const material_zone& zone, const hole_data& hole, const course_world_hole_start& start) {
    material_zone placed = zone;
    placed.center = place_hole_point(hole, start, zone.center);
    placed.rotation = zone.rotation + glm::radians(start.rotation_degrees);
    return placed;
}

play_area with_bounds(play_area area) {
    glm::vec3 low(std::numeric_limits<float>::max());
    glm::vec3 high(std::numeric_limits<float>::lowest());
    for (const terrain_vertex& vertex : area.ground.vertices) {
        low = glm::min(low, vertex.position);
        high = glm::max(high, vertex.position);
    }
    if (low.x > high.x) {
        return area;
    }
    area.center = horizontal((low + high) * 0.5f);
    area.extent = std::max(high.x - low.x, high.z - low.z) * 0.5f;

    glm::vec3 holes_low(std::numeric_limits<float>::max());
    glm::vec3 holes_high(std::numeric_limits<float>::lowest());
    for (const terrain_mesh& hole : area.holes) {
        for (const terrain_vertex& vertex : hole.vertices) {
            holes_low = glm::min(holes_low, vertex.position);
            holes_high = glm::max(holes_high, vertex.position);
        }
    }
    for (const material_zone& zone : area.zones) {
        const glm::vec2 half = zone_half_extent(zone);
        holes_low = glm::min(holes_low, zone.center - glm::vec3(half.x, 0.0f, half.y));
        holes_high = glm::max(holes_high, zone.center + glm::vec3(half.x, 0.0f, half.y));
    }
    if (holes_low.x <= holes_high.x) {
        area.holes_low = horizontal(holes_low);
        area.holes_high = horizontal(holes_high);
    }
    return area;
}

// Full ribbon width (fairway + rough); also how far the ground reaches past
// the holes.
float ribbon_width(const hole_data& hole) {
    return std::max(hole.spline.width, hole.spline.rough_width);
}

ground_settings ground_settings_for(const float margin, const game_tuning& tuning) {
    return ground_settings{tuning.terrain.ground_cell_size, margin, tuning.terrain.ground_blend_distance,
                           tuning.terrain.zone_cell_size, tuning.terrain.zones};
}

// Points around a pond's edge where its water level is found.
constexpr int water_rim_samples = 48;

glm::vec3 water_rim_point(const material_zone& zone, const int k) {
    const float angle = glm::two_pi<float>() * static_cast<float>(k) / static_cast<float>(water_rim_samples);
    return zone_world_point(zone, glm::vec2(std::cos(angle) * zone.radii.x, std::sin(angle) * zone.radii.y));
}

// Each water zone's level: the lowest point of the ground around its edge,
// where the bowl is uncarved, so the water fills the bowl without spilling
// over it. Water zones that overlap are one pond (a lake mapped as several
// ellipses) and share the lowest of their levels; the part of an edge inside
// another water zone is under that one's water, not a rim. Other zones get none.
std::vector<float> water_levels(const play_area& area) {
    const std::vector<material_zone>& zones = area.zones;
    std::vector<float> levels(zones.size(), 0.0f);
    std::vector<std::size_t> pond(zones.size());
    for (std::size_t i = 0; i < zones.size(); ++i) {
        pond[i] = i;
        if (zones[i].type != material_zone_type::water) {
            continue;
        }
        float lowest = std::numeric_limits<float>::max();
        for (int k = 0; k < water_rim_samples; ++k) {
            const glm::vec3 rim = water_rim_point(zones[i], k);
            const bool in_other_water = std::any_of(zones.begin(), zones.end(), [&](const material_zone& other) {
                return &other != &zones[i] && other.type == material_zone_type::water && zone_contains(other, rim);
            });
            if (!in_other_water) {
                lowest = std::min(lowest, sample_terrain_mesh(area.ground, rim, area.ground_y).point.y);
            }
        }
        levels[i] = lowest;
    }
    const auto root = [&pond](std::size_t i) {
        while (pond[i] != i) {
            i = pond[i];
        }
        return i;
    };
    for (std::size_t i = 0; i < zones.size(); ++i) {
        for (std::size_t j = i + 1; j < zones.size(); ++j) {
            if (zones[i].type != material_zone_type::water || zones[j].type != material_zone_type::water) {
                continue;
            }
            bool overlap = zone_contains(zones[j], zones[i].center) || zone_contains(zones[i], zones[j].center);
            for (int k = 0; k < water_rim_samples && !overlap; ++k) {
                overlap = zone_contains(zones[j], water_rim_point(zones[i], k));
            }
            if (overlap) {
                pond[root(j)] = root(i);
            }
        }
    }
    std::vector<float> lowest(zones.size(), std::numeric_limits<float>::max());
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (zones[i].type == material_zone_type::water) {
            lowest[root(i)] = std::min(lowest[root(i)], levels[i]);
        }
    }
    for (std::size_t i = 0; i < zones.size(); ++i) {
        if (zones[i].type == material_zone_type::water) {
            // A zone wholly inside others has no rim of its own; ground_y if none of the pond has.
            levels[i] = lowest[root(i)] < std::numeric_limits<float>::max() ? lowest[root(i)] : area.ground_y;
        }
    }
    return levels;
}

// The area with its ground built from its holes and zones, and the zone
// shapes draped on that ground for drawing.
play_area with_ground(play_area area, const height_grid* land, const float margin, const game_tuning& tuning) {
    area.ground = build_ground(area.holes, area.zones, land, ground_settings_for(margin, tuning));
    area.water_levels = water_levels(area);
    area.material_overlay = build_material_overlay_mesh(area.ground, area.zones, tuning.terrain.material_overlay_spacing);
    return area;
}

// The area with a tee box at each hole's tee, over its finished ground. Each
// faces the way its sign does.
play_area with_tee_boxes(play_area area, const std::vector<hole_data>& holes, const game_tuning& tuning) {
    std::vector<tee_box> boxes;
    for (const hole_data& hole : holes) {
        boxes.push_back(place_tee_box(area, hole, down_hole_at_tee(hole, tuning.hole_sign.look_ahead), tuning.tee_box));
    }
    area.tee_boxes = std::move(boxes);
    return area;
}

// The area with a sign at each hole's tee, standing on its finished ground.
play_area with_signs(play_area area, const std::vector<hole_data>& holes, const game_tuning& tuning) {
    area.signs = place_hole_signs(area, holes, tuning.hole_sign);
    area.sign_posts = hole_sign_posts(area.signs);
    return area;
}

// The area with `fences` standing on its finished ground.
play_area with_fences(play_area area, const std::vector<course_world_fence>& fences, const fence_tuning& tuning) {
    for (course_world_fence fence : fences) {
        for (glm::vec3& pole : fence.poles) {
            pole.y = terrain_height(area, pole);
            area.fence_poles.push_back(tree_body{pole, tree_shape{tuning.pole_radius, fence.height, 0.0f, 0.0f}});
        }
        for (std::size_t i = 0; i + 1 < fence.poles.size(); ++i) {
            area.fence_panels.push_back(fence_panel{fence.poles[i], fence.poles[i + 1], fence.height});
        }
        area.fences.push_back(std::move(fence));
    }
    return area;
}

// The hole's ribbon in hole space.
terrain_mesh build_hole_mesh(const hole_data& hole, const game_tuning& tuning) {
    terrain_spline spline;
    spline.control_points = hole.spline.control_points;
    spline.bank = hole.spline.bank;
    spline.width = ribbon_width(hole);
    spline.fairway_width = hole.spline.width;
    spline.sample_count = tuning.terrain.min_sections;
    return build_terrain_mesh(spline);
}

// Eases a placed hole's rough into the course's land: the hole's own height
// across the fairway, the land's height at the ribbon's outer edge, so
// neighbouring holes meet on the land rather than on each other's heights.
terrain_mesh fit_rough_to_land(terrain_mesh hole, const float fairway_width, const height_grid& land) {
    const float fairway_half = fairway_width * 0.5f;
    const float rough_band = std::max(0.001f, hole.width * 0.5f - fairway_half);
    for (terrain_vertex& vertex : hole.vertices) {
        const float t = clamp01((std::abs(vertex.distance_from_center) - fairway_half) / rough_band);
        const float land_height = sample_height_grid(land, vertex.position.x, vertex.position.z);
        vertex.position.y += t * t * (3.0f - 2.0f * t) * (land_height - vertex.position.y);
    }
    hole.vertices = with_smooth_normals(std::move(hole.vertices), hole.indices);
    return build_terrain_mesh_index(std::move(hole));
}
}

glm::vec3 place_hole_point(const hole_data& hole, const course_world_hole_start& start, const glm::vec3& point) {
    return start.position + rotate_about_y(point - hole.tee_position, glm::radians(start.rotation_degrees));
}

hole_data place_hole(const hole_data& hole, const course_world_hole_start& start) {
    hole_data placed = hole;
    placed.tee_position = place_hole_point(hole, start, hole.tee_position);
    placed.pin_position = place_hole_point(hole, start, hole.pin_position);
    for (glm::vec3& point : placed.spline.control_points) {
        point = place_hole_point(hole, start, point);
    }
    for (material_zone& zone : placed.material_zones) {
        zone = place_zone(zone, hole, start);
    }
    for (tree_instance& tree : placed.trees) {
        tree.position = place_hole_point(hole, start, tree.position);
    }
    return placed;
}

play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning) {
    play_area course;
    std::vector<hole_data> placed_holes;
    float margin = 0.0f;
    const height_grid* land = world.ground.heights.empty() ? nullptr : &world.ground;
    for (std::size_t i = 0; i < holes.size() && i < world.hole_starts.size(); ++i) {
        const hole_data placed = place_hole(holes[i], world.hole_starts[i]);
        terrain_mesh mesh = place_mesh(build_hole_mesh(holes[i], tuning), holes[i], world.hole_starts[i]);
        course.holes.push_back(land != nullptr ? fit_rough_to_land(std::move(mesh), holes[i].spline.width, *land) : std::move(mesh));
        course.zones.insert(course.zones.end(), placed.material_zones.begin(), placed.material_zones.end());
        course.trees.insert(course.trees.end(), placed.trees.begin(), placed.trees.end());
        margin = std::max(margin, ribbon_width(holes[i]));
        placed_holes.push_back(placed);
    }
    if (!placed_holes.empty()) {
        course.ground_y = placed_holes.front().tee_position.y;
    }
    play_area grounded = with_tee_boxes(with_bounds(with_ground(std::move(course), land, margin, tuning)),
                                        placed_holes, tuning);
    return with_fences(with_signs(std::move(grounded), placed_holes, tuning), world.fences, tuning.fence);
}

bool under_water(const play_area& area, const glm::vec3& position) {
    const terrain_sample sample = sample_area(area, position);
    return sample.material == terrain_material::water && position.y < sample.water_level;
}

terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    terrain_sample sample = sample_terrain_mesh(area.ground, position, area.ground_y);
    const std::optional<terrain_sample> on_hole = sample_holes(area.holes, position);
    sample.material = surface_material(area.zones, on_hole ? &*on_hole : nullptr, position);
    if (sample.material == terrain_material::water) {
        if (const std::optional<zone_hit> pond = winning_zone_at(area.zones, position); pond && pond->index < area.water_levels.size()) {
            sample.water_level = area.water_levels[pond->index];
        }
    }
    sample.distance_from_center = on_hole ? on_hole->distance_from_center : 0.0f;
    for (const tee_box& box : area.tee_boxes) {
        if (on_tee_box(box, position)) {
            sample.point.y = box.center.y;
            sample.normal = world_up;
            break;
        }
    }
    record_terrain_sample(profile, sample.triangles_tested);
    return sample;
}

float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    return sample_area(area, position, profile).point.y;
}

glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    const terrain_sample sample = sample_area(area, position, profile);
    return glm::vec3(position.x, sample.point.y, position.z);
}

glm::vec3 resting_ball_position(const play_area& area, const glm::vec3& position, const float radius) {
    const terrain_sample ground = sample_area(area, position);
    return ground.point + ground_normal(ground.normal) * radius;
}

glm::vec3 tee_stance_position(const play_area& area, const glm::vec3& ball, const glm::vec3& pin, const float stand_off) {
    glm::vec3 stance = ball - yaw_direction(yaw_towards(ball, pin)) * stand_off;
    stance.y = terrain_height(area, stance);
    return stance;
}

glm::vec3 address_stance_position(const play_area& area, const glm::vec3& ball, const float aim_angle,
                                  const player_tuning& player, frame_profile* profile) {
    const glm::vec3 forward = yaw_direction(aim_angle);
    glm::vec3 stance = ball + yaw_left(forward) * player.ball_stand_off_distance - forward * player.address_back_distance;
    stance.y = terrain_height(area, stance, profile);
    return stance;
}

bool on_cart_road(const std::vector<course_world_cart_road>& roads, const glm::vec3& position, const cart_tuning& cart) {
    const glm::vec3 point = horizontal(position);
    for (const course_world_cart_road& road : roads) {
        const float reach = std::max(cart.min_road_reach, road.width * 0.5f + cart.road_reach_margin);
        for (std::size_t i = 0; i + 1 < road.polyline.size(); ++i) {
            const glm::vec3 a = horizontal(road.polyline[i]);
            const glm::vec3 ab = horizontal(road.polyline[i + 1]) - a;
            const float length_squared = glm::dot(ab, ab);
            const float t = length_squared <= 0.0001f ? 0.0f : clamp01(glm::dot(point - a, ab) / length_squared);
            if (glm::length(point - (a + ab * t)) <= reach) {
                return true;
            }
        }
    }
    return false;
}

std::vector<tree_body> standing_trees(const play_area& area, frame_profile* profile) {
    std::vector<tree_body> trees;
    trees.reserve(area.trees.size());
    for (const tree_instance& tree : area.trees) {
        trees.push_back(tree_body{anchor_on_terrain(area, tree.position, profile), tree.shape});
    }
    return trees;
}
