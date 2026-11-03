#pragma once

// What a line of sight meets standing on a course, as the rangefinder's
// laser does (game/rangefinder.h): the area's solids (tree trunks and their
// leaves, the poles of floodlights, fences and signs, the signs' boards, the
// fences' nets; not flagsticks or tee boxes), carts and people. The ground is ground_hit's. Presentation only: no shot
// ever reads it. Pure.

#include "game/play_area.h"

#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// Upright, round: from `base` up `height`.
struct sight_post {
    glm::vec3 base{0.0f};
    float radius = 0.0f;
    float height = 0.0f;
};

// A box `half` its size along `along` (horizontal unit), up and across.
struct sight_box {
    glm::vec3 center{0.0f};
    glm::vec3 along{0.0f, 0.0f, 1.0f};
    glm::vec3 half{0.0f};
};

// What stands in view this frame: the area's own solids, with whatever moves
// (carts, people) gathered by the caller.
struct sight_scene {
    const play_area* area = nullptr;
    std::vector<sight_post> posts;
    std::vector<sight_box> boxes;
};

// How far along unit `direction` from `eye` the line first meets something
// standing, up to `max_distance` (world units).
std::optional<float> standing_hit(const sight_scene& scene, const glm::vec3& eye, const glm::vec3& direction,
                                  float max_distance);

// The same against one post or box.
std::optional<float> post_hit(const sight_post& post, const glm::vec3& eye, const glm::vec3& direction);
std::optional<float> box_hit(const sight_box& box, const glm::vec3& eye, const glm::vec3& direction);
