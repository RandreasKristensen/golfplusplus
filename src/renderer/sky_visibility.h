#pragma once

// What each spot of the ground sees of the sky, baked once when the area's
// render meshes are built, never per frame, in one march over one height
// grid: how much of the sky it sees (its ambient occlusion: bunkers, valleys
// and the foot of banks take less of the sky's light), and how high the land
// rises over it 8 ways, which hides the sun behind hills at low sun
// (lighting.glsl's sun_shadow). Pure and GL-free.

#include "physics/ground_mesh.h"
#include "physics/terrain.h"

#include <array>

#include <glm/vec3.hpp>

// Horizon way k looks along (cos k*45deg, 0, sin k*45deg): +x, then round towards +z.
inline constexpr int horizon_way_count = 8;

// `ground`'s heights on a grid every `spacing` metres over its XZ bounds
// (spacing grown so a side has at most max_sky_grid_side points): each point
// the height of the triangle over it, a point no triangle covers the lowest
// height, so a gap never shades its neighbours.
height_grid rasterize_ground_heights(const terrain_mesh& ground, float spacing);

// The ground's light, every grid on the grid of the heights it was baked
// from, for sample_height_grid at any spot.
struct ground_light_bake {
    // The share of the sky each point sees, 0..1: 1 on open level ground,
    // less the higher the ground around rises over it, out to a few tens of
    // metres.
    height_grid sky_visibility;
    // The sine of the horizon's elevation each horizon way, out to a few
    // hundred metres: 0 where nothing rises over the point that way.
    std::array<height_grid, horizon_way_count> horizons;
};

ground_light_bake bake_ground_light(const height_grid& heights);
ground_light_bake bake_ground_light(const terrain_mesh& ground);

// How much each horizon way counts towards the sun at `towards_sun`: the two
// ways either side of its azimuth, the nearer the more, summing to 1.
std::array<float, horizon_way_count> horizon_way_weights(const glm::vec3& towards_sun);

inline constexpr int max_sky_grid_side = 1024;
