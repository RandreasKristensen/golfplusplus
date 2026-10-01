#include "game/play_area.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

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
    for (const terrain_mesh* mesh : {&area.terrain, &area.apron}) {
        for (const terrain_vertex& vertex : mesh->vertices) {
            low = glm::min(low, vertex.position);
            high = glm::max(high, vertex.position);
        }
    }
    if (low.x > high.x) {
        return area;
    }
    area.center = horizontal((low + high) * 0.5f);
    area.extent = std::max(high.x - low.x, high.z - low.z) * 0.5f;
    return area;
}

// Full ribbon width (fairway + rough); also how far the apron reaches past it.
float ribbon_width(const hole_data& hole) {
    return std::max(hole.spline.width, hole.spline.rough_width);
}

terrain_mesh build_apron(const terrain_mesh& terrain, const float margin, const game_tuning& tuning) {
    return build_outer_rough_apron(terrain, margin, tuning.terrain.apron_cell_size, tuning.terrain.zones);
}

// The hole's ribbon and zone overlay, in hole space.
struct hole_meshes {
    terrain_mesh terrain;
    terrain_mesh overlay;
};

hole_meshes build_hole_meshes(const hole_data& hole, const game_tuning& tuning) {
    terrain_spline spline;
    spline.control_points = hole.spline.control_points;
    spline.width = ribbon_width(hole);
    spline.fairway_width = hole.spline.width;
    spline.sample_count = tuning.terrain.min_sections;

    hole_meshes meshes;
    meshes.terrain = build_terrain_mesh(spline, hole.material_zones, tuning.terrain.zones);
    meshes.overlay = build_material_overlay_mesh(meshes.terrain, hole.material_zones, tuning.terrain.material_overlay_lift);
    return meshes;
}

hole_meshes place_hole_meshes(const hole_data& hole, const course_world_hole_start& start, const game_tuning& tuning) {
    const hole_meshes meshes = build_hole_meshes(hole, tuning);
    return hole_meshes{place_mesh(meshes.terrain, hole, start), place_mesh(meshes.overlay, hole, start)};
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
    const hole_meshes meshes = build_hole_meshes(hole, tuning);
    play_area area;
    area.wind_seed = hole.wind_seed;
    area.ground_y = hole.tee_position.y;
    area.trees = hole.trees;
    area.terrain = meshes.terrain;
    area.apron = build_apron(area.terrain, ribbon_width(hole), tuning);
    area.material_overlay = meshes.overlay;
    return with_bounds(std::move(area));
}

play_area build_placed_hole_area(const hole_data& hole, const course_world_hole_start& start, const game_tuning& tuning) {
    hole_meshes meshes = place_hole_meshes(hole, start, tuning);
    play_area area;
    area.wind_seed = hole.wind_seed;
    area.ground_y = place_hole_point(hole, start, hole.tee_position).y;
    area.trees = place_hole(hole, start).trees;
    area.terrain = std::move(meshes.terrain);
    area.apron = build_apron(area.terrain, ribbon_width(hole), tuning);
    area.material_overlay = std::move(meshes.overlay);
    return with_bounds(std::move(area));
}

play_area build_hub_area(const std::vector<hole_data>& holes,
                         const course_world_definition& world,
                         const game_tuning& tuning) {
    play_area hub;
    float apron_margin = 0.0f;
    for (std::size_t i = 0; i < holes.size() && i < world.hole_starts.size(); ++i) {
        const hole_meshes hole = place_hole_meshes(holes[i], world.hole_starts[i], tuning);
        const std::vector<tree_instance> trees = place_hole(holes[i], world.hole_starts[i]).trees;
        hub.terrain = append_mesh(std::move(hub.terrain), hole.terrain);
        hub.material_overlay = append_mesh(std::move(hub.material_overlay), hole.overlay);
        hub.trees.insert(hub.trees.end(), trees.begin(), trees.end());
        apron_margin = std::max(apron_margin, ribbon_width(holes[i]));
    }
    hub.terrain = build_terrain_mesh_index(std::move(hub.terrain));
    hub.material_overlay = build_terrain_mesh_index(std::move(hub.material_overlay));
    // One apron over every hole: a per-hole apron would follow only its own
    // hole's heights and float over (or sink under) its neighbours.
    hub.apron = build_apron(hub.terrain, apron_margin, tuning);
    return with_bounds(std::move(hub));
}

terrain_sample sample_area(const play_area& area,
                           const glm::vec3& position,
                           frame_profile* profile,
                           const terrain_sample* previous_sample) {
    const terrain_sample sample = sample_terrain_mesh(area.terrain, position, area.ground_y, previous_sample);
    record_terrain_sample(profile, sample.triangles_tested);
    return sample;
}

float terrain_height(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    return sample_area(area, position, profile).point.y;
}

glm::vec3 anchor_on_terrain(const play_area& area, const glm::vec3& position, frame_profile* profile) {
    const terrain_sample sample = sample_terrain_anchor(area.terrain, position, area.ground_y);
    record_terrain_sample(profile, sample.triangles_tested);
    return sample.point;
}
