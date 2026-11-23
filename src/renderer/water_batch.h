#pragma once

// The ponds' surfaces as the scene draws them: the pond pieces of the zones'
// overlay (play_area::material_overlay), laid flat at their pond's level
// (play_area::water_levels), so each surface ends exactly on its pond's rounded
// outline, as fairways and greens do, and where physics puts the water. Each point
// keeps its fetch, the open water upwind of it, for each of the kept wind
// directions (renderer/water_light.h), so the waves grow away from the
// upwind shore whichever way the wind turns. Built when the play area changes.
// The triangles are kept grouped by their exact level and, within a level, by
// 32 m cell, so a level is one contiguous range and each cell cluster has its
// own bounds: candidates, queries, uploads and mirror crops read these instead
// of scanning triangles. GL-free; renderer/water_renderer draws it.

#include "game/play_area.h"
#include "renderer/water_light.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct render_water_vertex {
    glm::vec3 position{0.0f};  // at its pond's level
    // Metres of open water upwind when the wind blows towards each kept
    // direction, no more than water_fetch_cap_meters.
    float fetch[water_fetch_directions] = {};
};

// Triangles that share one cell of one level; the unit of bounds.
struct water_cluster {
    int first_vertex = 0;
    int vertex_count = 0;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

// Every triangle at exactly one physical level. Levels are never merged by
// closeness: two ponds are one level only when their heights are equal floats.
struct water_level_range {
    float level = 0.0f;
    int first_vertex = 0;
    int vertex_count = 0;
    int first_cluster = 0;
    int cluster_count = 0;
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};
};

struct render_water {
    std::vector<render_water_vertex> triangles;  // three points per triangle, sorted by level then cell
    std::vector<water_level_range> levels;       // ascending
    std::vector<water_cluster> clusters;         // by level, then cell
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
};

// The fetch kept is measured on a grid this fine, and no further: past it
// the waves are full whatever the style says.
inline constexpr float water_fetch_cell_meters = 1.0f;
inline constexpr float water_fetch_cap_meters = 200.0f;

inline constexpr float water_cluster_cell_meters = 32.0f;

// Sorts `water.triangles` and fills `levels` and `clusters` from them.
void index_render_water(render_water& water);

render_water build_render_water(const play_area& area, std::uint64_t revision);
