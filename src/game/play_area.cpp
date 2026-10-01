#include "game/play_area.h"

#include "physics/ground_mesh.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/common.hpp>
#include <glm/trigonometric.hpp>

namespace {
glm::vec3 rotate_y(const glm::vec3& point, const float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec3(point.x * c - point.z * s, point.y, point.x * s + point.z * c);
}

terrain_mesh place_mesh(const terrain_mesh& mesh, const hole_data& hole, const course_world_hole_start& start) {
    terrain_mesh placed = mesh;
    const float radians = glm::radians(start.rotation_degrees);
    for (terrain_vertex& vertex : placed.vertices) {
        vertex.position = place_hole_point(hole, start, vertex.position);
        vertex.normal = rotate_y(vertex.normal, radians);
    }
    return build_terrain_mesh_index(std::move(placed));
}

material_zone place_zone(const material_zone& zone, const hole_data& hole, const course_world_hole_start& start) {
    material_zone placed = zone;
    placed.center = place_hole_point(hole, start, zone.center);
    if (zone.has_bounds) {
        // A rotated box is approximated by its axis-aligned bounds. Only the
        // placed hole_data uses this; terrain is always built in hole space.
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? zone.bounds_max.x : zone.bounds_min.x,
                                  (corner & 2) ? zone.bounds_max.y : zone.bounds_min.y,
                                  (corner & 4) ? zone.bounds_max.z : zone.bounds_min.z);
            const glm::vec3 world = place_hole_point(hole, start, local);
            low = glm::min(low, world);
            high = glm::max(high, world);
        }
        placed.bounds_min = low;
        placed.bounds_max = high;
    }
    return placed;
}

// Appends `source` to `target`. Ribbons must share cross_section_count so the
// combined mesh keeps a consistent row layout (see terrain_mesh).
terrain_mesh append_mesh(terrain_mesh target, const terrain_mesh& source) {
    if (source.vertices.empty()) {
        return target;
    }
    if (target.vertices.empty()) {
        target.cross_section_count = source.cross_section_count;
    }
    const std::uint32_t offset = static_cast<std::uint32_t>(target.vertices.size());
    target.vertices.insert(target.vertices.end(), source.vertices.begin(), source.vertices.end());
    for (const std::uint32_t index : source.indices) {
        target.indices.push_back(offset + index);
    }
    target.section_count += source.section_count;
    target.width = std::max(target.width, source.width);
    return target;
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

// `overlay` moved onto the ground, `lift` above it, keeping its XZ shapes.
terrain_mesh drape_on_ground(terrain_mesh overlay, const terrain_mesh& ground, const float lift) {
    for (terrain_vertex& vertex : overlay.vertices) {
        const terrain_sample sample = sample_terrain_anchor(ground, vertex.position, vertex.position.y);
        vertex.position.y = sample.point.y + lift;
        vertex.normal = sample.normal;
    }
    return build_terrain_mesh_index(std::move(overlay));
}

// The hole's ribbon and zone overlay, in hole space. `level` is the same
// ribbon without zones (same vertex order): its base shape, before carving.
struct hole_meshes {
    terrain_mesh terrain;
    terrain_mesh level;
    terrain_mesh overlay;
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
    meshes.overlay = build_material_overlay_mesh(meshes.terrain, hole.material_zones, tuning.terrain.material_overlay_lift);
    return meshes;
}

hole_meshes place_hole_meshes(const hole_data& hole, const course_world_hole_start& start, const game_tuning& tuning) {
    const hole_meshes meshes = build_hole_meshes(hole, tuning);
    return hole_meshes{place_mesh(meshes.terrain, hole, start), place_mesh(meshes.level, hole, start),
                       place_mesh(meshes.overlay, hole, start)};
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
    return start.position + rotate_y(point - hole.tee_position, glm::radians(start.rotation_degrees));
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
    hole_meshes meshes = build_hole_meshes(hole, tuning);
    play_area area;
    area.ground_y = hole.tee_position.y;
    area.trees = hole.trees;
    area.holes.push_back(std::move(meshes.terrain));
    area.ground = build_ground(area.holes, nullptr, ground_settings_for(ribbon_width(hole), tuning));
    area.material_overlay = drape_on_ground(std::move(meshes.overlay), area.ground, tuning.terrain.material_overlay_lift);
    return with_bounds(std::move(area));
}

play_area build_course_area(const std::vector<hole_data>& holes,
                            const course_world_definition& world,
                            const game_tuning& tuning) {
    play_area course;
    terrain_mesh overlay;
    float margin = 0.0f;
    for (std::size_t i = 0; i < holes.size() && i < world.hole_starts.size(); ++i) {
        hole_meshes hole = place_hole_meshes(holes[i], world.hole_starts[i], tuning);
        const std::vector<tree_instance> trees = place_hole(holes[i], world.hole_starts[i]).trees;
        course.holes.push_back(fit_rough_to_land(std::move(hole.terrain), hole.level, holes[i].spline.width, world.ground));
        overlay = append_mesh(std::move(overlay), hole.overlay);
        course.trees.insert(course.trees.end(), trees.begin(), trees.end());
        margin = std::max(margin, ribbon_width(holes[i]));
    }
    course.ground = build_ground(course.holes, &world.ground, ground_settings_for(margin, tuning));
    course.material_overlay = drape_on_ground(std::move(overlay), course.ground, tuning.terrain.material_overlay_lift);
    return with_bounds(std::move(course));
}

terrain_sample sample_area(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    terrain_sample sample = sample_terrain_mesh(area.ground, position, area.ground_y);
    const std::optional<terrain_sample> on_hole = sample_holes(area.holes, position);
    sample.material = on_hole ? on_hole->material : terrain_material::rough;
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
