#pragma once

// The ground of a play area: one surface that every height and normal comes
// from, for physics and drawing alike. It is a regular grid. Inside a hole the
// hole's ribbon decides the height; where holes overlap they blend, weighted
// towards the hole a point is deeper inside; away from the holes the ground
// eases from the nearest hole edge into the land (a course world's height
// grid), or keeps the edge height when there is no land. Materials come from
// the exact zone shapes and the holes' fairway and rough (surface_material),
// never from the grid, so their edges stay exact.

#include "physics/terrain.h"

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// Heights on a regular XZ grid, row by row along z, `columns` along x.
// heights[row * columns + column] is at (origin_x + column * cell_size,
// origin_z + row * cell_size).
struct height_grid {
    float origin_x = 0.0f;
    float origin_z = 0.0f;
    float cell_size = 0.0f;
    int columns = 0;
    int rows = 0;
    std::vector<float> heights;
};

// Bilinear height at (x, z), clamped to the grid's edge outside it; 0 for an
// empty grid.
float sample_height_grid(const height_grid& grid, float x, float z);

struct ground_settings {
    float cell_size = 0.0f;       // grid spacing
    float margin = 0.0f;          // how far the grid reaches past the holes
    float blend_distance = 0.0f;  // from a hole's edge into the land
};

// `holes` are ribbon meshes (each with its spatial index), `zones` every
// hole's zones in the same coordinates; `land` may be null. The grid covers
// every hole plus the margin, and all of `land`. Its vertices carry
// surface_material, for drawing.
terrain_mesh build_ground(const std::vector<terrain_mesh>& holes,
                          const std::vector<material_zone>& zones,
                          const height_grid* land,
                          const ground_settings& settings);

// The holes' sample at `position`, nothing when it is off every hole. Where
// holes overlap, fairway wins over rough and the hole the point is deeper
// inside wins a tie.
std::optional<terrain_sample> sample_holes(const std::vector<terrain_mesh>& holes, const glm::vec3& position);

// The material at `position`: the winning zone's (zone_material_at, which
// reaches past the ribbons), else the holes' fairway or rough, else rough.
// `on_hole` is the holes' winning sample there, null when off every hole.
terrain_material surface_material(const std::vector<material_zone>& zones,
                                  const terrain_sample* on_hole,
                                  const glm::vec3& position);
