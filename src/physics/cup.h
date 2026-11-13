#pragma once

// A hole cut in a green: within `radius` of its centre the ground gives way
// to a plumb cylinder `depth` deep. Its lip is the cut edge of the green,
// lying in the green's plane (`up` at the hole), worn round by `rim_radius`
// below the surface, so nothing stands above the grass. The ball rolls over
// the lip, falls in when slow enough to drop below it before the far side,
// lips out riding the rim when fast or off centre, and can rattle out off the
// wall or climb it on its topspin. Penner's capture speeds
// (tests/fixtures/reference/cup.json) are what it is checked against. Pure.

#include "physics/ball_state.h"

#include <glm/vec3.hpp>

struct cup {
    glm::vec3 centre{0.0f};  // on the green, at the middle of the hole
    float radius = 0.0f;
    float depth = 0.0f;
    float rim_radius = 0.0f;
    glm::vec3 up{0.0f, 1.0f, 0.0f};  // the green's normal there
};

// How the lip, the wall and the bottom give the ball back. The grips are
// coefficients of friction against the spinning ball (a rolling ball's
// topspin climbs the back wall); the bottom just takes `liner_friction` of
// its sliding.
struct cup_contact_tuning {
    float rim_restitution = 0.0f;
    float rim_grip = 0.0f;
    float wall_grip = 0.0f;
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

// The green's height at `point`'s horizontal position, in its plane at the hole.
float lip_height(const cup& hole, const glm::vec3& point);

// Down in the hole: inside it and wholly below its lip.
bool in_cup(const ball_state& ball, const cup& hole);

// Where a ball of `radius` rests on the bottom of the hole, on its axis.
glm::vec3 cup_rest(const cup& hole, float radius);
