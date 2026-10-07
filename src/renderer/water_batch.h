#pragma once

// The ponds' surfaces as the scene draws them: every ground triangle with a
// corner on water, laid flat at its pond's level (play_area::water_levels).
// One surface per pond however many zones it was mapped as; past the shore
// the bank rises above it and hides it. Built when the play area changes.
// GL-free; renderer/water_renderer draws it.

#include "game/play_area.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct render_water {
    std::vector<glm::vec3> triangles;  // three points per triangle, all at a pond's level
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
};

render_water build_render_water(const play_area& area, std::uint64_t revision);
