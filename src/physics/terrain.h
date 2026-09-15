#pragma once

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

#include "physics/material_zone.h"

struct terrain_spline {
    std::vector<glm::vec3> control_points;
    // Width is the full rendered/playable ribbon. fairway_width marks the inner fairway strip.
    float width = 0.0f;
    float fairway_width = 0.0f;
    int sample_count = 96;
};

enum class terrain_material {
    fairway,
    rough,
    green,
    bunker,
    water
};

struct terrain_zone_tuning {
    float bunker_depth = 0.55f;
    float water_depth = 0.35f;
};

struct terrain_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance_from_center = 0.0f;
    terrain_material material = terrain_material::fairway;
};

// Deterministic uniform XZ grid over the mesh triangles, stored CSR style.
// It is purely an acceleration structure: sampling must produce the same result
// with or without it, and an empty/stale index simply falls back to a full scan.
struct terrain_mesh_index {
    float min_x = 0.0f;
    float min_z = 0.0f;
    float max_x = 0.0f;
    float max_z = 0.0f;
    float cell_size_x = 0.0f;
    float cell_size_z = 0.0f;
    int cells_x = 0;
    int cells_z = 0;
    // Cheap staleness fingerprint. If a caller copies a mesh and moves its
    // vertices without rebuilding the index, these stop matching and sampling
    // falls back to the exact full scan instead of returning wrong results.
    uint32_t vertex_count = 0;
    uint32_t triangle_count = 0;
    glm::vec3 fingerprint_first{0.0f};
    glm::vec3 fingerprint_middle{0.0f};
    glm::vec3 fingerprint_last{0.0f};
    std::vector<uint32_t> cell_starts;    // size cells_x * cells_z + 1
    std::vector<uint32_t> cell_triangles; // ascending triangle indices per cell
};

struct terrain_mesh {
    std::vector<terrain_vertex> vertices;
    std::vector<uint32_t> indices;
    int section_count = 0;
    int cross_section_count = 0;
    float width = 0.0f;
    terrain_mesh_index spatial_index;
};

struct terrain_sample {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 barycentric{0.0f};
    float distance_from_center = 0.0f;
    int triangle_index = -1;
    // Number of mesh triangles actually tested to produce this sample.
    // Pure output value, useful for profiling the spatial index.
    int triangles_tested = 0;
    terrain_material material = terrain_material::rough;
    bool has_spline = false;
    bool inside_surface = false;
};

glm::vec3 sample_terrain_spline_point(const terrain_spline& terrain, float t);
terrain_mesh build_terrain_mesh(const terrain_spline& terrain);
terrain_mesh build_terrain_mesh(const terrain_spline& terrain,
                                const std::vector<material_zone>& zones,
                                const terrain_zone_tuning& tuning);
terrain_mesh build_material_overlay_mesh(const terrain_mesh& source_mesh,
                                         const std::vector<material_zone>& zones,
                                         float lift);
terrain_mesh build_outer_rough_apron(const terrain_mesh& mesh, float margin, int grid_resolution);
// Returns a copy of the mesh with its spatial index rebuilt from its current
// vertices/indices. All build_*_mesh functions already do this; callers that
// assemble or transform a terrain_mesh by hand should run the result through
// this so sampling keeps the fast path.
terrain_mesh build_terrain_mesh_index(terrain_mesh mesh);
terrain_sample sample_terrain_mesh(const terrain_mesh& mesh, const glm::vec3& position, float fallback_y);
terrain_sample sample_terrain_mesh(const terrain_mesh& mesh,
                                   const glm::vec3& position,
                                   float fallback_y,
                                   const terrain_sample* previous_sample);
terrain_sample sample_terrain_anchor(const terrain_mesh& mesh, const glm::vec3& position, float fallback_y);
terrain_sample sample_terrain(const terrain_spline& terrain, const glm::vec3& position, float fallback_y);
