#pragma once

// The coloured screen edges that running event effects ask for (game/effect_catalog.h
// screen_vignette), as of the shared clock: full where the effect started,
// fading evenly to nothing over the vignette's seconds. Presentation only: it
// reads the running effects and never touches gameplay. Pure.

#include "game/effect_catalog.h"
#include "game/effects.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

// 1 at the start, 0 at `seconds` and after, falling evenly; 0 before the start.
float vignette_strength(std::int64_t started_at_ms, std::int64_t now_ms, float seconds);

struct vignette_look {
    glm::vec3 colour{0.0f};
    float strength = 0.0f;  // 0..1, above 0
};

// One for each event of `catalog` with a vignette that runs among `effects`
// at `now_ms` and has not faded out.
std::vector<vignette_look> running_vignettes(const std::vector<active_effect>& effects, const effect_catalog& catalog,
                                             std::int64_t now_ms);
