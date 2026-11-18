#pragma once

// A tree grown in code from its species' look (lighting.json "trees"), GL-free
// and the same every run for a seed. In a unit frame: the trunk's foot at the
// origin, the whole tree 1 tall, its crown 1 wide each way of its middle;
// an instance scales it to its own height and crown. The trunk rises
// trunk_share of the way and limbs reach out from it into the crown's
// shape; leaf clumps fill the crown near its surface, each joined to its
// nearest limb by a twig, so a crown out of leaf is its branches. A clump
// is crossed cards whose leaves the shader draws; their normals point out
// from the crown's middle, so the crown is lit as one soft mass, and each
// carries how deep in the crown it is (inner leaves are shaded). Bark comes
// first in the vertices, then leaves, so the bark can be drawn alone.

#include "game/lighting_loader.h"

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct grown_tree_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};  // a leaf card's corner, 0..1; unused on bark
    float leaf = 0.0f;   // 1 on a leaf card, 0 on bark
    float depth = 0.0f;  // 0 at the crown's surface .. 1 at its middle
};

struct grown_tree {
    std::vector<grown_tree_vertex> vertices;  // triangles: bark, then leaves
    std::size_t bark_count = 0;               // the first bark_count vertices
};

grown_tree grow_tree(const tree_look& look, std::uint32_t seed);

// Where a point of the unit frame lies within `look`'s crown shape: 0 at its
// middle, 1 on its surface, more outside. Unit tested.
float crown_reach(const tree_look& look, const glm::vec3& point);
