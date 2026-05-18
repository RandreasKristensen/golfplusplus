#include "physics/collision.h"

#include <algorithm>

namespace {
float clamp01(const float value) {
    return std::max(0.0f, std::min(1.0f, value));
}
}

ball_state resolve_ground_collision(const ball_state in, const float ground_y, const float restitution, const float friction) {
    ball_state out = in;
    if (out.position.y >= ground_y) {
        return out;
    }

    out.position.y = ground_y;
    const float clamped_restitution = clamp01(restitution);
    if (out.velocity.y < 0.0f) {
        out.velocity.y = -out.velocity.y * clamped_restitution;
    }

    const float clamped_friction = clamp01(friction);
    out.velocity.x *= 1.0f - clamped_friction;
    out.velocity.z *= 1.0f - clamped_friction;

    return out;
}
