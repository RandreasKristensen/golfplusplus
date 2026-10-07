#include "physics/fence_collision.h"

#include "physics/collision.h"
#include "physics/vector_math.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
// Pushed this little past touching, so the bounce still registers when the
// swept contact lands exactly a radius off the net.
constexpr float contact_slop = 0.0001f;
}

ball_state resolve_fence_collision(const glm::vec3& previous_position,
                                   const ball_state& in,
                                   const fence_panel& panel,
                                   const float restitution,
                                   const float friction) {
    const glm::vec3 along = horizontal(panel.b - panel.a);
    const float length = glm::length(along);
    if (length < 0.0001f || panel.height <= 0.0f) {
        return in;
    }
    const glm::vec3 u = along / length;
    const glm::vec3 n(-u.z, 0.0f, u.x);
    const float radius = std::max(0.0f, in.radius);

    // Signed distances from the net's line before and after the step, on the
    // side the ball came from.
    const float before = glm::dot(horizontal(previous_position - panel.a), n);
    const float side = before > 0.0f ? 1.0f : (before < 0.0f ? -1.0f : (glm::dot(in.velocity, n) > 0.0f ? -1.0f : 1.0f));
    const float from = side * before;
    const float to = side * glm::dot(horizontal(in.position - panel.a), n);
    if (to >= radius) {
        return in;  // ends the step clear of the net, on its own side
    }

    // Where the path first comes within a radius of the net (where the step
    // starts, when it already touches it).
    const float t = from > radius ? (from - radius) / (from - to) : 1.0f;
    const glm::vec3 contact = previous_position + (in.position - previous_position) * clamp01(t);
    const float across = glm::dot(horizontal(contact - panel.a), u);
    if (across < 0.0f || across > length) {
        return in;  // past the end poles (poles are their own obstacles)
    }
    const float foot = panel.a.y + (panel.b.y - panel.a.y) * (across / length);
    if (contact.y < foot - radius || contact.y > foot + panel.height + radius) {
        return in;  // under the ground or over the top
    }

    ball_state out = in;
    out.position = contact;
    const float distance = side * glm::dot(horizontal(contact - panel.a), n);
    return resolve_contact(out, n * side, radius - distance + contact_slop, restitution, friction);
}

ball_state resolve_fence_collisions(const glm::vec3& previous_position,
                                    const ball_state& in,
                                    const std::vector<fence_panel>& panels,
                                    const float restitution,
                                    const float friction) {
    // Each panel is swept from the step's start to wherever the ball ends by
    // then, so a panel the path reaches sooner than an earlier hit wins.
    ball_state out = in;
    for (const fence_panel& panel : panels) {
        out = resolve_fence_collision(previous_position, out, panel, restitution, friction);
    }
    return out;
}
