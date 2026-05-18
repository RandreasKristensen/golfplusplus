#include "physics/collision.h"

ball_state resolve_ground_collision(const ball_state in, const float ground_y, const float restitution, const float friction) {
    ball_state out = in;
    if (out.position.y >= ground_y) {
        return out;
    }

    out.position.y = ground_y;
    if (out.velocity.y < 0.0f) {
        out.velocity.y = -out.velocity.y * restitution;
    }

    const float clamp_friction = friction < 0.0f ? 0.0f : (friction > 1.0f ? 1.0f : friction);
    out.velocity.x *= 1.0f - clamp_friction;
    out.velocity.z *= 1.0f - clamp_friction;

    return out;
}
