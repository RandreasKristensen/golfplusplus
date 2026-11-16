#pragma once

// The ponds' surfaces as the scene draws them: the part below its pond's
// level (play_area::water_levels) of every ground triangle with a corner on
// water, laid flat at that level, so each surface ends where the bank meets
// it. One surface per pond however many zones it was mapped as. Each point
// keeps its fetch, the open water upwind of it, for each of the kept wind
// directions (renderer/water_light.h), so the waves grow away from the
// upwind shore whichever way the wind turns. Built when the play area changes.
// GL-free; renderer/water_renderer draws it.

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

struct render_water {
    std::vector<render_water_vertex> triangles;  // three points per triangle
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
};

// The fetch kept is measured on a grid this fine, and no further: past it
// the waves are full whatever the style says.
inline constexpr float water_fetch_cell_meters = 1.0f;
inline constexpr float water_fetch_cap_meters = 200.0f;

render_water build_render_water(const play_area& area, std::uint64_t revision);
