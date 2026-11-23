#pragma once

// GL-free: a tree's branches grown in code from its species' look
// (lighting.json "trees"), the same every run for a seed, in the unit frame
// renderer/tree_model.h builds the model in: the trunk's foot at the origin,
// the whole tree 1 tall, its crown 1 wide each way of its middle.
//
// Space colonization (Runions, Lane and Prusinkiewicz 2007): attraction points
// fill the crown, denser toward its surface, and branches grow from the trunk
// toward them until every point is reached, bending toward the light and
// drooping as the species says. Its thickness is the pipe model's (each
// parent's radius^n the sum of its children's), and it is collapsed into stems
// by order: the trunk, its limbs or leaders, and branches (at most three
// orders). Leaf clumps grow from the branch ends and the finest branches' sides.

#include "game/lighting_loader.h"

#include <cstdint>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

// The grown branches, in the unit frame. A stem is a run of nodes of one
// order, from the node it leaves its parent at to its end.
struct tree_skeleton {
    struct node {
        glm::vec3 position{0.0f};
        int parent = -1;
        float radius = 0.0f;
    };
    struct stem {
        std::vector<int> nodes;
        int order = 0;  // 0 the trunk (a bush's stems), 1 limbs and leaders, 2 branches
    };
    // A leaf clump: where it leaves its branch and the way it grows from there.
    struct clump {
        glm::vec3 base{0.0f};
        glm::vec3 direction{0.0f, 1.0f, 0.0f};
    };
    std::vector<node> nodes;
    std::vector<stem> stems;  // coarse orders first
    std::vector<clump> clumps;
};

tree_skeleton grow_skeleton(const tree_look& look, std::uint32_t seed);

// A box in the unit frame: its centre and half sizes.
struct tree_box {
    glm::vec3 centre{0.0f};
    glm::vec3 half{0.0f};
};

// The box of `look`'s crown shape (a bush's dome reaches under the ground).
tree_box crown_shape_box(const tree_look& look);

// The box `look`'s crown fills above the ground.
tree_box tree_crown_box(const tree_look& look);

// Where a point of the unit frame lies within `look`'s crown shape: 0 at its
// middle, 1 on its surface, more outside. Unit tested.
float crown_reach(const tree_look& look, const glm::vec3& point);

// Deterministic 0..1 numbers (splitmix64), the same on every platform.
class tree_dice {
public:
    explicit tree_dice(const std::uint64_t seed) : state_(seed * 0x9e3779b97f4a7c15ULL + 0x632be59bd9b4e019ULL) {}

    float next() {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        z ^= z >> 31;
        return static_cast<float>(z >> 40) / static_cast<float>(1ULL << 24);
    }

    float between(const float low, const float high) { return low + (high - low) * next(); }

private:
    std::uint64_t state_;
};

// The tree grows in a frame of crown radii every way ("true" frame): the unit
// frame with its height stretched by the look's aspect, so branches and cards
// come out round once an instance scales the unit frame.
struct tree_true_frame {
    float aspect = 1.0f;
    glm::vec3 unit(const glm::vec3& p) const { return glm::vec3(p.x, p.y / aspect, p.z); }
    glm::vec3 lifted(const glm::vec3& p) const { return glm::vec3(p.x, p.y * aspect, p.z); }
    // A true-frame normal as the unit frame stores it (tree.vert divides by the instance's scale).
    glm::vec3 unit_normal(const glm::vec3& n) const { return glm::normalize(glm::vec3(n.x, n.y * aspect, n.z)); }
};
