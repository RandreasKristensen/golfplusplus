#pragma once

// How a ball leaves the club: what a launch monitor measures, in real units.
// The flight takes it from here (ball_physics.h).

#include "physics/ball_state.h"

#include <glm/vec3.hpp>

struct launch_conditions {
    float ball_speed = 0.0f;      // m/s
    float launch_angle = 0.0f;    // radians above level
    float direction = 0.0f;       // world yaw it starts along (vector_math.h yaw_direction)
    float spin = 0.0f;            // rad/s
    // Radians the spin axis tilts from pure backspin about the launch
    // direction: positive curves the ball left, negative right.
    float spin_axis_tilt = 0.0f;
};

// A ball of `radius` at `position` leaving as `launch` says.
ball_state launched_ball(const glm::vec3& position, float radius, const launch_conditions& launch);
