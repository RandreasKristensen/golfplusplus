#pragma once

// The floodlights (game/floodlights.h) as the scene draws them: a six-sided
// pole up to an arm and a lamp head boxed round its lamp, the head's face
// towards what it lights and glowing in lighting.glsl's lamp_glow after dark.
// Built when the play area changes. GL-free; renderer/floodlight_renderer
// draws it, and into the shadow map.

#include "game/lighting_loader.h"
#include "game/play_area.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct floodlight_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{0.0f};
    float lamp = 0.0f;  // 1 on a lamp's face: it glows of itself
};

struct render_floodlights {
    std::vector<floodlight_vertex> vertices;  // triangles
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
};

render_floodlights build_render_floodlights(const play_area& area, std::uint64_t revision, const fixture_colors& colors);
