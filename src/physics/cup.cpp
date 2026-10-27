#include "physics/cup.h"

#include "physics/collision.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
// Off the cup's axis, horizontally (any way when on it).
glm::vec3 outwards(const ball_state& ball, const cup& hole) {
    return safe_normalize(horizontal(ball.position - hole.centre), glm::vec3(1.0f, 0.0f, 0.0f));
}
}

bool over_cup(const ball_state& ball, const cup& hole) {
    return horizontal_distance(ball.position, hole.centre) < hole.radius;
}

cup_step cup_contact(const ball_state& in, const cup& hole, const cup_contact_tuning& tuning) {
    cup_step out{in};
    ball_state& ball = out.ball;
    const float radius = std::max(0.0f, in.radius);
    const float reach = horizontal_distance(in.position, hole.centre);
    if (reach > hole.radius + hole.rim_radius + radius || in.position.y > hole.centre.y + hole.rim_radius + radius) {
        return out;
    }
    // The rim's edge: a ring round the hole at the green's height.
    const glm::vec3 edge = hole.centre + outwards(ball, hole) * hole.radius;
    const glm::vec3 from_edge = ball.position - edge;
    const float edge_distance = glm::length(from_edge);
    if (edge_distance < radius + hole.rim_radius && edge_distance > 0.0f) {
        const glm::vec3 normal = from_edge / edge_distance;
        ball = resolve_contact(ball, normal, radius + hole.rim_radius - edge_distance, tuning.rim_restitution,
                               tuning.rim_friction);
        out.touched = true;
        out.on_rim = true;
    }
    // Below the rim: the wall all round and the bottom.
    const float bottom = hole.centre.y - hole.depth;
    if (ball.position.y < hole.centre.y && horizontal_distance(ball.position, hole.centre) < hole.radius) {
        const float off_axis = horizontal_distance(ball.position, hole.centre);
        if (off_axis + radius > hole.radius) {
            ball = resolve_contact(ball, -outwards(ball, hole), off_axis + radius - hole.radius, tuning.liner_restitution,
                                   tuning.liner_friction);
            out.touched = true;
        }
        if (ball.position.y - radius < bottom) {
            ball = resolve_contact(ball, glm::vec3(0.0f, 1.0f, 0.0f), bottom - (ball.position.y - radius),
                                   tuning.liner_restitution, tuning.liner_friction);
            out.touched = true;
            out.supported = true;
        }
    }
    return out;
}

bool in_cup(const ball_state& ball, const cup& hole) {
    return horizontal_distance(ball.position, hole.centre) < hole.radius && ball.position.y + ball.radius < hole.centre.y;
}
