#include "game/play_area.h"

#include "physics/ground_mesh.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
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

// The zone where the placed hole puts it: a box keeps its size and turns
// with the hole around its own (placed) centre.
material_zone place_zone(const material_zone& zone, const hole_data& hole, const course_world_hole_start& start) {
    material_zone placed = zone;
    placed.center = place_hole_point(hole, start, zone.center);
    if (zone.has_bounds) {
        const glm::vec3 center = (zone.bounds_min + zone.bounds_max) * 0.5f;
        const glm::vec3 shift = place_hole_point(hole, start, center) - center;
        placed.bounds_min = zone.bounds_min + shift;
        placed.bounds_max = zone.bounds_max + shift;
        placed.rotation = zone.rotation + glm::radians(start.rotation_degrees);
    }
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
    return area;
}

// Full ribbon width (fairway + rough); also how far the ground reaches past
// the holes.
float ribbon_width(const hole_data& hole) {
    return std::max(hole.spline.width, hole.spline.rough_width);
}

ground_settings ground_settings_for(const float margin, const game_tuning& tuning) {
    return ground_settings{tuning.terrain.ground_cell_size, margin, tuning.terrain.ground_blend_distance};
}

// The area with its ground built from its holes and zones, and the zone
// shapes draped on that ground for drawing.
play_area with_ground(play_area area, const height_grid* land, const float margin, const game_tuning& tuning) {
    area.ground = build_ground(area.holes, area.zones, land, ground_settings_for(margin, tuning));
    area.material_overlay = build_material_overlay_mesh(area.ground, area.zones, tuning.terrain.material_overlay_lift,
                                                        tuning.terrain.material_overlay_spacing);
    return area;
}

// The hole's ribbon in hole space. `level` is the same ribbon without zones
// (same vertex order): its base shape, before carving.
struct hole_meshes {
    terrain_mesh terrain;
    terrain_mesh level;
};

hole_meshes build_hole_meshes(const hole_data& hole, const game_tuning& tuning) {
    terrain_spline spline;
    spline.control_points = hole.spline.control_points;
    spline.bank = hole.spline.bank;
    spline.width = ribbon_width(hole);
    spline.fairway_width = hole.spline.width;
    spline.sample_count = tuning.terrain.min_sections;

    hole_meshes meshes;
    meshes.terrain = build_terrain_mesh(spline, hole.material_zones, tuning.terrain.zones);
    meshes.level = build_terrain_mesh(spline, {}, tuning.terrain.zones);
    return meshes;
}

hole_meshes place_hole_meshes(const hole_data& hole, const course_world_hole_start& start, const game_tuning& tuning) {
    const hole_meshes meshes = build_hole_meshes(hole, tuning);
    return hole_meshes{place_mesh(meshes.terrain, hole, start), place_mesh(meshes.level, hole, start)};
}

// Eases a placed hole's rough into the course's land: the hole's own height
// across the fairway, the land's height at the ribbon's outer edge, so
// neighbouring holes meet on the land rather than on each other's heights.
// The shift is taken from the level ribbon, so bunkers and water keep their
// carve wherever they sit.
terrain_mesh fit_rough_to_land(terrain_mesh hole, const terrain_mesh& level, const float fairway_width, const height_grid& land) {
    const float fairway_half = fairway_width * 0.5f;
    const float rough_band = std::max(0.001f, hole.width * 0.5f - fairway_half);
    for (std::size_t i = 0; i < hole.vertices.size() && i < level.vertices.size(); ++i) {
        terrain_vertex& vertex = hole.vertices[i];
        const float t = clamp01((std::abs(vertex.distance_from_center) - fairway_half) / rough_band);
        const float base = level.vertices[i].position.y;
        vertex.position.y += t * t * (3.0f - 2.0f * t) * (sample_height_grid(land, vertex.position.x, vertex.position.z) - base);
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

play_area build_hole_area(const hole_data& hole, const game_tuning& tuning) {
    play_area area;
    area.ground_y = hole.tee_position.y;
    area.trees = hole.trees;
    area.holes.push_back(build_hole_meshes(hole, tuning).terrain);
    area.zones = hole.material_zones;
    return with_bounds(with_ground(std::move(area), nullptr, ribbon_width(hole), tuning));
}

play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning) {
    play_area course;
    float margin = 0.0f;
    for (std::size_t i = 0; i < holes.size() && i < world.hole_starts.size(); ++i) {
        hole_meshes hole = place_hole_meshes(holes[i], world.hole_starts[i], tuning);
        const hole_data placed = place_hole(holes[i], world.hole_starts[i]);
        course.holes.push_back(fit_rough_to_land(std::move(hole.terrain), hole.level, holes[i].spline.width, world.ground));
        course.zones.insert(course.zones.end(), placed.material_zones.begin(), placed.material_zones.end());
        course.trees.insert(course.trees.end(), placed.trees.begin(), placed.trees.end());
        margin = std::max(margin, ribbon_width(holes[i]));
    }
    return with_bounds(with_ground(std::move(course), &world.ground, margin, tuning));
}

terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    terrain_sample sample = sample_terrain_mesh(area.ground, position, area.ground_y);
    const std::optional<terrain_sample> on_hole = sample_holes(area.holes, position);
    sample.material = surface_material(area.zones, on_hole ? &*on_hole : nullptr, position);
    sample.distance_from_center = on_hole ? on_hole->distance_from_center : 0.0f;
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
