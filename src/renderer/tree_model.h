#pragma once

// A tree's model grown in code from its species' look (lighting.json "trees"),
// GL-free and the same every run for a seed, in the unit frame an instance
// scales to its own height and crown: its skeleton (renderer/tree_skeleton.h)
// as bent tubes by order (6 / 4 / 3 sided), the branch ends narrowing to
// their sprays' twigs. Leaf clumps sit at the branch ends: crossed cards
// fanned along the branch's end, on which the shader draws
// leaves, withered leaves and a spray of fine twigs (tree_leaf.glsl). Their
// normals point out from the crown's middle, so the crown is lit as one soft
// mass, and each carries how deep in the crown it is (inner leaves are
// shaded) and how readily it keeps withered leaves in winter (low and inner
// clumps most). Bark comes first in the indices, coarse orders first, then the
// cards. Vertices with identical attributes are stored once.

#include "game/lighting_loader.h"
#include "renderer/tree_skeleton.h"

#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct grown_tree_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};  // a card's corner, 0..1, v along its branch; unused on bark
    float leaf = 0.0f;   // 1 on a card, 0 on bark
    float depth = 0.0f;  // a card's: 0 at the crown's surface .. 1 at its middle; bark's: its order, 0 trunk .. 1 branches
    float hold = 0.0f;   // a card's share of the crown's withered leaves, 0.5 on average
    float seed = 0.0f;   // a card's own 0..1, so no two sprays are the same
};

struct grown_tree {
    std::vector<grown_tree_vertex> vertices;  // unique, bark's first
    std::vector<std::uint32_t> indices;       // triangles: bark, then cards
};

grown_tree grow_tree(const tree_look& look, std::uint32_t seed);

// The bark orders' prism sides: the trunk, limbs, branches.
inline constexpr int tree_order_sides[3] = {6, 4, 3};

// The box round every vertex of a model, in its unit frame.
tree_box tree_box_of(const grown_tree& model);
