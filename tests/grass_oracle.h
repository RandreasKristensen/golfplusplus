#pragma once

// The grass field's full-precision oracle: the whole lattice rasterized at once
// into float triples (height, distance to bare ground, kind), with the bare
// distance found by brute force over every bare edge. Slow and dense on
// purpose; production bakes a tile at a time (renderer/grass_baker.h).

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/tee_box.h"
#include "renderer/render_mesh.h"

struct dense_grass {
    glm::vec2 origin{0.0f};
    int columns = 0;
    int rows = 0;
    std::vector<glm::vec3> samples;  // row by row; x height, y distance, z kind
    const glm::vec3& node(int x, int z) const { return samples[static_cast<std::size_t>(z) * columns + x]; }
};

dense_grass bake_dense_grass(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees);
// The bilinear value at a root, as the field read at full precision; zero past it.
glm::vec3 sample_dense_grass(const dense_grass& field, const glm::vec2& root);
