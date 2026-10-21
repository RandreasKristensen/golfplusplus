#pragma once

// A prop's parts (game/prop_loader.h) as unit meshes placed in the world:
// what the world marker batch and the emote props draw. A box is its six
// faces. GL-free.

#include "game/prop_loader.h"

#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

enum class prop_mesh {
    cylinder,  // unit cylinder, y in [0, 1]
    sphere,    // unit sphere
    quad       // unit quad, [-1, 1] in XY
};

struct prop_piece {
    prop_mesh mesh = prop_mesh::sphere;
    glm::mat4 model{1.0f};
    glm::vec3 color{0.0f};
    const prop_part* part = nullptr;  // its glow and tag
};

// Every part of `model` in `frame` (the prop's own frame in the world).
std::vector<prop_piece> prop_pieces(const prop_model& model, const glm::mat4& frame);
// How many pieces prop_pieces gives for `model`.
std::size_t prop_piece_count(const prop_model& model, prop_mesh mesh);
