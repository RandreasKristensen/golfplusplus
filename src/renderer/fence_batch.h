#pragma once

// The course's fences as the scene draws them: a square post at every pole
// and the net between each pair, textured with assets/textures/fence_net.bmp
// so it reads as strands up close and a faint veil far off. Built when the
// play area changes. GL-free; renderer/fence_renderer draws it.

#include "game/play_area.h"

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// Metres of net one copy of the net texture covers; keep it in step with
// NET_TILE_METRES in tooling/art/make_art.py.
inline constexpr float net_tile_metres = 1.0f;

struct fence_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{0.0f};  // the posts' colour (nets take the texture's)
    glm::vec2 uv{0.0f};     // net texture coordinates, in tiles
};

struct render_fences {
    std::vector<fence_vertex> posts;  // opaque triangles
    std::vector<fence_vertex> nets;   // see-through triangles, drawn after everything solid
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
};

render_fences build_render_fences(const play_area& area, std::uint64_t revision);
