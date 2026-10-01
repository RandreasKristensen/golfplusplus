#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

#include "physics/material_zone.h"

// Terrain is a ribbon mesh built along a Catmull-Rom spline. Each "section" is
// one row of `cross_section_count` vertices across the ribbon.
struct terrain_spline {
    std::vector<glm::vec3> control_points;
    // Side slope per control point (empty = level): metres of rise per metre
    // towards the lateral side (-tangent.z, 0, tangent.x), the player's right
    // looking down the hole. Interpolated along the hole, so a ribbon on a
    // hillside tilts with the hill.
    std::vector<float> bank;
    // Full ribbon width (fairway + rough). `fairway_width` is the inner strip.
    float width = 0.0f;
    float fairway_width = 0.0f;
    // Minimum number of sections along the spline (more are added for long holes).
    int sample_count = 0;
};

// Underlying values index per-material arrays; keep `terrain_material_count` in sync.
enum class terrain_material : std::uint8_t {
    fairway,
    rough,
    green,
    bunker,
    water
};
inline constexpr std::size_t terrain_material_count = 5;

// A ribbon's rough edge sits up to this much lower than its fairway.
inline constexpr float ribbon_edge_drop = 0.18f;

struct terrain_zone_tuning {
    float bunker_depth = 0.0f;
    float water_depth = 0.0f;
};

struct terrain_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    // Signed lateral offset from the spline centreline (0 for non-ribbon meshes
    // except where noted by the builder).
    float distance_from_center = 0.0f;
    terrain_material material = terrain_material::fairway;
};

// Deterministic uniform XZ grid over the mesh triangles, stored CSR style.
// Purely an acceleration structure: sampling gives the same result with or
// without it, and an empty or stale index falls back to a full scan.
struct terrain_mesh_index {
    float min_x = 0.0f;
    float min_z = 0.0f;
    float max_x = 0.0f;
    float max_z = 0.0f;
    float cell_size_x = 0.0f;
    float cell_size_z = 0.0f;
    int cells_x = 0;
    int cells_z = 0;
    // Staleness fingerprint: if a mesh is copied and its vertices moved without
    // rebuilding the index, these stop matching and sampling does a full scan.
    std::uint32_t vertex_count = 0;
    std::uint32_t triangle_count = 0;
    glm::vec3 fingerprint_first{0.0f};
    glm::vec3 fingerprint_middle{0.0f};
    glm::vec3 fingerprint_last{0.0f};
    std::vector<std::uint32_t> cell_starts;     // size cells_x * cells_z + 1
    std::vector<std::uint32_t> cell_triangles;  // ascending triangle indices per cell
};

struct terrain_mesh {
    std::vector<terrain_vertex> vertices;
    std::vector<std::uint32_t> indices;
    // Ribbon layout: vertex rows of `cross_section_count` vertices. Several
    // ribbons with the same cross_section_count can be appended into one mesh
    // (the course hub). Zero for meshes that are not ribbons.
    int section_count = 0;
    int cross_section_count = 0;
    float width = 0.0f;
    terrain_mesh_index spatial_index;
};

struct terrain_sample {
    glm::vec3 point{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float distance_from_center = 0.0f;
    // -1 when the mesh was empty and `point` is the fallback height.
    int triangle_index = -1;
    // Triangles tested to produce this sample (for profiling the index).
    int triangles_tested = 0;
    terrain_material material = terrain_material::rough;
    // False when no triangle contains the query and the nearest edge was used.
    bool inside_surface = false;
};

glm::vec3 sample_terrain_spline_point(const terrain_spline& terrain, float t);

// Builds the ribbon mesh with material zones applied (bunkers and water are
// carved by `tuning`). Includes the spatial index.
terrain_mesh build_terrain_mesh(const terrain_spline& terrain,
                                const std::vector<material_zone>& zones,
                                const terrain_zone_tuning& tuning);

// Zone shapes draped over `source_mesh` and lifted by `lift`, for drawing.
terrain_mesh build_material_overlay_mesh(const terrain_mesh& source_mesh,
                                         const std::vector<material_zone>& zones,
                                         float lift);

// Two triangles per grid cell over `rows` x `columns` vertices laid out row by
// row starting at `first_vertex`.
std::vector<std::uint32_t> grid_triangle_indices(int rows, int columns, std::uint32_t first_vertex);

// `vertices` with area-weighted upward normals from the triangles that use them.
std::vector<terrain_vertex> with_smooth_normals(std::vector<terrain_vertex> vertices,
                                                const std::vector<std::uint32_t>& indices);

// Returns the mesh with its spatial index rebuilt. Run any mesh assembled or
// transformed by hand through this so sampling keeps the fast path.
terrain_mesh build_terrain_mesh_index(terrain_mesh mesh);

// Height, normal and material at `position` (XZ). Off the mesh, the nearest
// edge is used and the material is rough. `previous_sample` (optional) keeps
// the result on the same ribbon where ribbons overlap.
terrain_sample sample_terrain_mesh(const terrain_mesh& mesh,
                                   const glm::vec3& position,
                                   float fallback_y,
                                   const terrain_sample* previous_sample = nullptr);

// The containing triangle's sample, or nothing when `position` is off the
// surface. Unlike sample_terrain_mesh it never searches for the nearest edge,
// so it stays cheap far from the mesh.
std::optional<terrain_sample> sample_terrain_inside(const terrain_mesh& mesh, const glm::vec3& position);

// Like sample_terrain_mesh, but keeps the query's exact XZ (only the height
// comes from the terrain). Used to place objects that sit on the ground.
terrain_sample sample_terrain_anchor(const terrain_mesh& mesh, const glm::vec3& position, float fallback_y);
