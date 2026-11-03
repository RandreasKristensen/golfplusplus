#pragma once

// A golf cart rolling over onto its side, one fixed step at a time
// (cart_fall_step_seconds). It turns about the line its wheels on that side
// touch the ground along, its weight at its centre of gravity (cg_height_m
// up, half its track in from that line): held up until the centre passes
// over the line, then pulled down by gravity until its side meets the
// ground a quarter turn over, where it bounces back by `restitution` of the
// speed it hit at and comes to rest. Its body turns as a box the track wide
// and twice the cg height tall. Radians and seconds. Pure.

#include "physics/cart_dynamics.h"

inline constexpr float cart_fall_step_seconds = 1.0f / 120.0f;

// How far over it is from upright on its wheels (lying on its side at a
// quarter turn), and how fast it turns that way.
struct cart_fall {
    float angle = 0.0f;
    float rate = 0.0f;
    bool resting = false;
};

// Lying on its side, a quarter turn over.
inline constexpr float cart_lying_angle = 1.5707963f;

// Its wheels just lifted with the speed that carries it over its tipping
// point (a tip only starts once the pull has beaten its stability).
cart_fall start_cart_fall(const cart_spec& spec);
cart_fall step_cart_fall(const cart_fall& in, const cart_spec& spec, float restitution, float dt);
