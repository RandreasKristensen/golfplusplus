#include "physics/cart_rollover.h"

#include "physics/det_math.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float gravity = 9.81f;
// Over its tipping point by this share of the speed that just reaches it.
constexpr float start_margin = 1.15f;
// Bouncing back slower than this, it lies still.
constexpr float rest_rate = 0.3f;

// From the line it turns about to its centre of gravity: half the track
// across, the cg height up.
float lever(const cart_spec& spec) {
    const float across = 0.5f * spec.track_m;
    return std::sqrt(across * across + spec.cg_height_m * spec.cg_height_m);
}

// The angle past which its centre is over that line.
float tipping_angle(const cart_spec& spec) {
    return det_atan2(0.5f * spec.track_m, spec.cg_height_m);
}

// Its turning inertia about that line, over its mass: its centre's, and a
// box the track wide and twice the cg height tall turning about its middle.
float inertia_per_mass(const cart_spec& spec) {
    const float l = lever(spec);
    const float height = 2.0f * spec.cg_height_m;
    return l * l + (spec.track_m * spec.track_m + height * height) / 12.0f;
}
}

cart_fall start_cart_fall(const cart_spec& spec) {
    const float rise = lever(spec) * (1.0f - det_cos(tipping_angle(spec)));
    cart_fall fall;
    fall.rate = start_margin * std::sqrt(2.0f * gravity * rise / inertia_per_mass(spec));
    return fall;
}

cart_fall step_cart_fall(const cart_fall& in, const cart_spec& spec, const float restitution, const float dt) {
    if (in.resting) {
        return in;
    }
    cart_fall out = in;
    const float pull = gravity * lever(spec) * det_sin(in.angle - tipping_angle(spec)) / inertia_per_mass(spec);
    out.rate = in.rate + pull * dt;
    out.angle = in.angle + out.rate * dt;
    if (out.angle >= cart_lying_angle) {
        out.angle = cart_lying_angle;
        out.rate = -restitution * std::max(0.0f, out.rate);
        out.resting = -out.rate < rest_rate;
        if (out.resting) {
            out.rate = 0.0f;
        }
    }
    // Once over its tipping point it never rolls back onto its wheels: a
    // bounce off its side is small.
    if (in.angle >= tipping_angle(spec)) {
        out.angle = std::max(out.angle, tipping_angle(spec));
    }
    return out;
}
