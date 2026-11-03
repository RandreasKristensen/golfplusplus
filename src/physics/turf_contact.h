#pragma once

// The ball against the ground: a bounce off turf that gives under it, a skid
// that turns into a roll, and the roll itself. Penner's compliant-bounce
// model (tests/fixtures/reference/ground.json): the turf's effective plane
// tilts back towards the incoming ball by a contact angle that grows with
// impact speed and angle, restitution falls with impact speed, and friction
// at the contact point trades speed for spin both ways (wedges check and
// spin back, a low-spinning drive skips on). Rolling, a ball loses
// (5/7) rho_r g with Penner's stimp resistance, and a slope pulls it at
// (5/7) g sin(slope). Sand catches a fast landing outright (it plugs). Pure.

#include "physics/ball_state.h"
#include "physics/terrain.h"

// How one surface takes the ball, wet or dry already blended in.
struct turf {
    float contact_angle_scale = 1.0f;  // of Penner's green contact angle: softer ground craters more
    float restitution_scale = 1.0f;    // of Penner's green restitution
    float friction = 0.0f;             // at the contact point
    float stimp_feet = 0.0f;           // rolling resistance as a stimpmeter reading
    float grass_drag = 0.0f;           // more rolling deceleration per m/s of speed through long grass (1/s)
    // Sand: landing into it faster than this (m/s, along the normal) it
    // plugs, sunk plug_depth; 0 never plugs.
    float plug_speed = 0.0f;
    float plug_depth = 0.0f;
};

// Penner's restitution of a green for an impact at `speed` m/s.
float green_restitution(float speed);
// Penner's effective contact angle (radians) for an impact at `speed` m/s
// and `angle` radians below the surface.
float green_contact_angle(float speed, float angle);
// m/s^2 a ball rolling at `speed` loses on a surface of `stimp_feet`.
float rolling_deceleration(float speed, float stimp_feet);

struct turf_step {
    ball_state ball;
    bool bounced = false;  // it struck the ground this step
    bool plugged = false;  // it buried itself in sand: it stays where it is
    bool resting = false;  // still on the ground and the slope cannot move it
};

// `in` (already moved by the step's flight, gravity included) against the
// ground under it, `ground`, over `dt`. Faster into the ground than
// `settle_speed` it bounces; slower it skids and rolls. Untouched off the ground.
turf_step turf_contact(const ball_state& in, const terrain_sample& ground, const turf& surface, float settle_speed,
                       float dt);
