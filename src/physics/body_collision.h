#pragma once

// Things standing on the ground (trunks, poles, flagsticks, nets, boards,
// blocks) as a body moving over the ground meets them: seen from above, in
// XZ only, whatever their height. A body is circles round its position;
// moved, it slides along whatever it presses against. Pure.

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// A trunk or pole, round.
struct upright_post {
    glm::vec2 center{0.0f};
    float radius = 0.0f;
};

// A net or board: a line from `a` to `b`, `half_thickness` either side of it.
struct upright_wall {
    glm::vec2 a{0.0f};
    glm::vec2 b{0.0f};
    float half_thickness = 0.0f;
};

// A solid rectangle: `half_length` along `along` (a unit vector) and
// `half_width` across it either side of `center`.
struct upright_block {
    glm::vec2 center{0.0f};
    glm::vec2 along{0.0f, 1.0f};
    float half_length = 0.0f;
    float half_width = 0.0f;
};

struct upright_obstacles {
    std::vector<upright_post> posts;
    std::vector<upright_wall> walls;
    std::vector<upright_block> blocks;
};

// A body moving over the ground: circles of `radius` at `offsets` (XZ) from
// its position. Pressed against something, it loses `friction` (0 to 1) of
// its motion along it: 0 slides freely, 1 sticks.
struct ground_body {
    std::vector<glm::vec2> offsets;
    float radius = 0.0f;
    float friction = 0.0f;
};

struct body_move {
    glm::vec3 position{0.0f};  // where it ended up; y as the move's `to`
    bool touched = false;      // it pressed against something on the way
};

// `body` moved from `from` to `to` (XZ) in steps of at most half its radius,
// pushed clear of every set in `obstacles` after each step.
body_move move_body(const std::vector<const upright_obstacles*>& obstacles, const ground_body& body,
                    const glm::vec3& from, const glm::vec3& to);
