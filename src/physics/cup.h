#pragma once

// A hole cut in a green: within `radius` of its centre the ground gives way
// to a cylinder `depth` deep, its rim's edge rounded by `rim_radius`. The
// ball rolls over the rim edge, falls in when slow enough to drop below it
// before the far side, lips out riding the rim when fast or off centre, and
// can rattle out off the wall. Holmes's capture speeds
// (tests/fixtures/reference/cup.json) are what it is checked against. Pure.

#include "physics/ball_state.h"

#include <glm/vec3.hpp>

struct cup {
    glm::vec3 centre{0.0f};  // on the green, at the middle of the hole
    float radius = 0.0f;
    float depth = 0.0f;
    float rim_radius = 0.0f;
};

// How the rim and the liner give the ball back.
struct cup_contact_tuning {
    float rim_restitution = 0.0f;
    float rim_friction = 0.0f;
    float liner_restitution = 0.0f;
    float liner_friction = 0.0f;
};

// The ball's centre is over the hole: the green does not hold it there.
bool over_cup(const ball_state& ball, const cup& hole);

struct cup_step {
    ball_state ball;
    bool touched = false;    // it met the rim, the wall or the bottom
    bool on_rim = false;     // it rides the rim's edge, the green's cut grass
    bool supported = false;  // on the bottom: nothing but the cup holds it
};

// `in` against the cup's rim edge, wall and bottom.
cup_step cup_contact(const ball_state& in, const cup& hole, const cup_contact_tuning& tuning);

// Down in the hole: inside it and wholly below its rim.
bool in_cup(const ball_state& ball, const cup& hole);
