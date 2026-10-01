#pragma once

// The ground around and between terrain ribbons: a regular grid that meets
// each ribbon's edge, sinks out of sight under it, and away from the ribbons
// follows the land (a course world's height grid) when there is one. It is a
// surface like the ribbons: the play area samples it wherever no ribbon is.

#include "physics/terrain.h"

#include <vector>

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

// A ground grid around `ribbons`, `margin` wider on each side, with vertices
// about `cell_size` apart and no land of its own: away from the ribbons it
// keeps the height of the nearest ribbon edge. Used for a single hole.
terrain_mesh build_outer_rough_apron(const terrain_mesh& ribbons,
                                     float margin,
                                     float cell_size,
                                     const terrain_zone_tuning& zones);

// The ground of a whole course: covers `land` and every ribbon, with vertices
// about `cell_size` apart. Next to a ribbon it meets the ribbon's edge, and
// over `blend_distance` it eases into the land's own height.
terrain_mesh build_course_ground(const terrain_mesh& ribbons,
                                 const height_grid& land,
                                 float cell_size,
                                 float blend_distance,
                                 const terrain_zone_tuning& zones);
