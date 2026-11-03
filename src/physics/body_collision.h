#pragma once

// Solids (physics/solid.h) as a body moving over the ground meets them: seen
// from above, in XZ only, whatever their height, but for a walkable top no
// higher than the body's step-up over its feet, which it steps onto. A body
// is a circle (a person) or an oriented box (a cart); moved, it slides along
// whatever it presses against. Up and down it falls, lands and follows the
// surface under it (settle_vertical). Pure.

#include "physics/solid.h"

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

enum class body_shape {
    circle,  // `radius` round its position
    box      // `half_length` along `along` and `half_width` across, its middle `centre_ahead` along `along` from its position
};

// A body moving over the ground. Pressed against something, it loses
// `friction` (0 to 1) of its motion along it: 0 slides freely, 1 sticks.
struct ground_body {
    body_shape shape = body_shape::circle;
    float radius = 0.0f;
    glm::vec2 along{0.0f, 1.0f};  // XZ unit
    float half_length = 0.0f;
    float half_width = 0.0f;
    float centre_ahead = 0.0f;
    float friction = 0.0f;
    // A walkable top up to this far over its feet does not stop it.
    float step_up = 0.0f;
};

ground_body circle_body(float radius, float friction, float step_up);
ground_body box_body(const glm::vec2& along, float half_length, float half_width, float centre_ahead, float friction,
                     float step_up);

struct body_move {
    glm::vec3 position{0.0f};  // where it ended up; y as the move's `to`
    bool touched = false;      // it pressed against something on the way
    // The way (XZ unit) what it met pushed it back, zero when it went on
    // unhindered, and the restitution of the bounciest thing it met.
    glm::vec2 normal{0.0f};
    float restitution = 0.0f;
};

// What a body bumps into: the area's solids on their grid, then `moving`
// (carts, people), each only where it stops `mover`.
struct body_obstacles {
    const std::vector<solid>& solids;
    const solid_grid& grid;
    const std::vector<solid>& moving;
    solid_mover mover = solid_mover::walker;
};

// `body` moved from `from` to `to` (XZ; its feet at from.y) in steps of at
// most half its narrowest size, pushed clear of every obstacle that stops it
// after each step: circles against every shape, a box against every shape
// by separating axes.
body_move move_body(const body_obstacles& obstacles, const ground_body& body, const glm::vec3& from, const glm::vec3& to);

// A body's height: its feet, moving up (positive) or down at `speed`, on
// the surface or in the air.
struct vertical_body {
    float y = 0.0f;
    float speed = 0.0f;
    bool grounded = true;
};

// `in` over `dt` above a surface at `surface`: on it, it follows the surface
// up and down by as much as `step_down` (slopes, steps); off a higher edge,
// or rising, it is in the air, pulled down by `gravity` until it comes down
// on the surface.
vertical_body settle_vertical(const vertical_body& in, float surface, float step_down, float gravity, float dt);
