#include "physics/body_collision.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
// Pushes per step: enough to settle a body caught between two obstacles.
constexpr int clear_passes = 3;
// However far one move goes, it takes at most this many steps.
constexpr int max_steps = 256;
// No step goes further, so a thin pole never ends a step inside a body.
constexpr float max_step_length = 0.05f;

glm::vec2 flat(const glm::vec3& point) {
    return glm::vec2(point.x, point.z);
}

glm::vec2 across_of(const glm::vec2& along) {
    return glm::vec2(along.y, -along.x);
}

// An oriented rectangle seen from above.
struct flat_box {
    glm::vec2 centre{0.0f};
    glm::vec2 along{0.0f, 1.0f};
    float half_length = 0.0f;
    float half_width = 0.0f;
};

// How far a circle at `center` must move to be `reach` from `point`.
glm::vec2 push_from(const glm::vec2& center, const glm::vec2& point, const float reach) {
    const glm::vec2 away = center - point;
    const float distance = glm::length(away);
    if (distance >= reach) {
        return glm::vec2(0.0f);
    }
    const glm::vec2 direction = distance > 1.0e-5f ? away / distance : glm::vec2(1.0f, 0.0f);
    return direction * (reach - distance);
}

glm::vec2 nearest_on_wall(const glm::vec2& a, const glm::vec2& b, const glm::vec2& point) {
    const glm::vec2 span = b - a;
    const float length_squared = glm::dot(span, span);
    if (length_squared <= 1.0e-10f) {
        return a;
    }
    return a + span * std::clamp(glm::dot(point - a, span) / length_squared, 0.0f, 1.0f);
}

// How far a circle at `center` of `radius` must move to be clear of `box`:
// out through its nearest side when its centre is inside.
glm::vec2 circle_from_box(const glm::vec2& center, const float radius, const flat_box& box) {
    const glm::vec2 across = across_of(box.along);
    const glm::vec2 local = center - box.centre;
    const float u = glm::dot(local, box.along);
    const float v = glm::dot(local, across);
    if (std::abs(u) < box.half_length && std::abs(v) < box.half_width) {
        const float out_along = box.half_length - std::abs(u);
        const float out_across = box.half_width - std::abs(v);
        return out_along < out_across ? box.along * (std::copysign(out_along, u) + std::copysign(radius, u))
                                      : across * (std::copysign(out_across, v) + std::copysign(radius, v));
    }
    const glm::vec2 nearest = box.centre + box.along * std::clamp(u, -box.half_length, box.half_length) +
        across * std::clamp(v, -box.half_width, box.half_width);
    return push_from(center, nearest, radius);
}

// How far `box` must move to be clear of `other`, by separating axes: along
// the axis they overlap least on, away from it; nothing when one separates them.
glm::vec2 box_from_box(const flat_box& box, const flat_box& other) {
    const glm::vec2 axes[4] = {box.along, across_of(box.along), other.along, across_of(other.along)};
    const glm::vec2 between = box.centre - other.centre;
    float least = 0.0f;
    glm::vec2 push(0.0f);
    for (int i = 0; i < 4; ++i) {
        const glm::vec2& axis = axes[i];
        const float reach = box.half_length * std::abs(glm::dot(box.along, axis)) +
            box.half_width * std::abs(glm::dot(across_of(box.along), axis)) +
            other.half_length * std::abs(glm::dot(other.along, axis)) +
            other.half_width * std::abs(glm::dot(across_of(other.along), axis));
        const float apart = glm::dot(between, axis);
        const float overlap = reach - std::abs(apart);
        if (overlap <= 0.0f) {
            return glm::vec2(0.0f);
        }
        if (i == 0 || overlap < least) {
            least = overlap;
            push = axis * (apart < 0.0f ? -overlap : overlap);
        }
    }
    return push;
}

// How far `box` must move to be clear of a circle at `center` of `radius`.
glm::vec2 box_from_circle(const flat_box& box, const glm::vec2& center, const float radius) {
    return -circle_from_box(center, radius, box);
}

flat_box solid_box(const solid& thing) {
    return flat_box{flat(thing.base), glm::normalize(flat(thing.along)), thing.half_length, thing.half_width};
}

// A wall seen from above as the box it fills (its ends are poles of their own).
flat_box wall_box(const solid& wall) {
    const glm::vec2 a = flat(wall.base);
    const glm::vec2 b = flat(wall.end);
    const float length = glm::length(b - a);
    const glm::vec2 along = length > 1.0e-5f ? (b - a) / length : glm::vec2(0.0f, 1.0f);
    return flat_box{0.5f * (a + b), along, 0.5f * length, wall.radius};
}

glm::vec2 push_circle(const glm::vec2& center, const float radius, const solid& thing) {
    switch (thing.shape) {
    case solid_shape::cylinder:
    case solid_shape::cone:
        return push_from(center, flat(thing.base), thing.radius + radius);
    case solid_shape::wall:
        return push_from(center, nearest_on_wall(flat(thing.base), flat(thing.end), center), thing.radius + radius);
    case solid_shape::box:
        return circle_from_box(center, radius, solid_box(thing));
    }
    return glm::vec2(0.0f);
}

glm::vec2 push_box(const flat_box& box, const solid& thing) {
    switch (thing.shape) {
    case solid_shape::cylinder:
    case solid_shape::cone:
        return box_from_circle(box, flat(thing.base), thing.radius);
    case solid_shape::wall:
        return box_from_box(box, wall_box(thing));
    case solid_shape::box:
        return box_from_box(box, solid_box(thing));
    }
    return glm::vec2(0.0f);
}

// `body` at `at` (its position) pushed clear of `thing`.
glm::vec2 push_body(const ground_body& body, const glm::vec2& at, const solid& thing) {
    if (body.shape == body_shape::circle) {
        return push_circle(at, body.radius, thing);
    }
    return push_box(flat_box{at + body.along * body.centre_ahead, body.along, body.half_length, body.half_width}, thing);
}

// It stops the body: of its mover, and not a walkable top within the
// body's step-up of its feet.
bool blocks(const solid& thing, const body_obstacles& obstacles, const ground_body& body, const float feet) {
    return stops(thing, obstacles.mover) && !(thing.walkable && solid_top(thing) <= feet + body.step_up);
}

// The body pushed clear of everything that stops it, in order; of what it
// was pushed from, the bounciest's restitution into `restitution`.
glm::vec2 push_clear(const body_obstacles& obstacles, const std::vector<std::uint32_t>& near, const ground_body& body,
                     const glm::vec2& at, const float feet, float& restitution) {
    glm::vec2 moved = at;
    const auto push = [&](const solid& thing) {
        if (!blocks(thing, obstacles, body, feet)) {
            return;
        }
        const glm::vec2 out = push_body(body, moved, thing);
        if (out != glm::vec2(0.0f)) {
            moved += out;
            restitution = std::max(restitution, thing.restitution);
        }
    };
    for (const std::uint32_t index : near) {
        push(obstacles.solids[index]);
    }
    for (const solid& thing : obstacles.moving) {
        push(thing);
    }
    return moved - at;
}

// How far the body reaches from its position, and its narrowest half.
float body_reach(const ground_body& body) {
    return body.shape == body_shape::circle ? body.radius
                                            : std::abs(body.centre_ahead) + std::hypot(body.half_length, body.half_width);
}

float body_narrowest(const ground_body& body) {
    return body.shape == body_shape::circle ? body.radius : std::min(body.half_length, body.half_width);
}
}

ground_body circle_body(const float radius, const float friction, const float step_up) {
    ground_body body;
    body.radius = radius;
    body.friction = friction;
    body.step_up = step_up;
    return body;
}

ground_body box_body(const glm::vec2& along, const float half_length, const float half_width, const float centre_ahead,
                     const float friction, const float step_up) {
    ground_body body;
    body.shape = body_shape::box;
    body.along = along;
    body.half_length = half_length;
    body.half_width = half_width;
    body.centre_ahead = centre_ahead;
    body.friction = friction;
    body.step_up = step_up;
    return body;
}

body_move move_body(const body_obstacles& obstacles, const ground_body& body, const glm::vec3& from, const glm::vec3& to) {
    const glm::vec2 travel = flat(to) - flat(from);
    const float step_length = std::clamp(0.5f * body_narrowest(body), 0.01f, max_step_length);
    const int steps = std::clamp(static_cast<int>(std::ceil(glm::length(travel) / step_length)), 1, max_steps);
    // Everything the body could reach on the way, pushes included.
    const float reach = body_reach(body);
    const float margin = reach + static_cast<float>(2 * clear_passes) * (obstacles.grid.reach + reach);
    const std::vector<std::uint32_t> near = solids_near(obstacles.grid, glm::min(from, to), glm::max(from, to), margin);

    // Where the steps alone take it, plus what pushes and friction add.
    glm::vec2 held(0.0f);
    glm::vec2 at = flat(from);
    bool touched = false;
    float restitution = 0.0f;
    const glm::vec2 step_travel = travel / static_cast<float>(steps);
    const float friction = std::clamp(body.friction, 0.0f, 1.0f);
    for (int step = 0; step < steps; ++step) {
        const glm::vec2 stepped = flat(from) + travel * (static_cast<float>(step + 1) / static_cast<float>(steps)) + held;
        at = stepped;
        for (int pass = 0; pass < clear_passes; ++pass) {
            const glm::vec2 push = push_clear(obstacles, near, body, at, from.y, restitution);
            if (push == glm::vec2(0.0f)) {
                break;
            }
            at += push;
            touched = true;
        }
        // Of the step's motion along what it pressed against, friction
        // holds some back.
        const glm::vec2 push = at - stepped;
        const float push_length = glm::length(push);
        if (push_length > 0.0f && friction > 0.0f) {
            const glm::vec2 normal = push / push_length;
            at -= (step_travel - normal * glm::dot(step_travel, normal)) * friction;
        }
        held += at - stepped;
    }
    const glm::vec2 pushed = at - (flat(from) + travel);
    const float pushed_length = glm::length(pushed);
    return body_move{glm::vec3(at.x, to.y, at.y), touched,
                     pushed_length > 1.0e-6f ? pushed / pushed_length : glm::vec2(0.0f), restitution};
}

vertical_body settle_vertical(const vertical_body& in, const float surface, const float step_down, const float gravity,
                              const float dt) {
    vertical_body out = in;
    if (in.grounded && in.speed <= 0.0f) {
        if (surface >= in.y - step_down) {
            return vertical_body{surface, 0.0f, true};
        }
        out.grounded = false;
    }
    out.y = in.y + in.speed * dt - 0.5f * gravity * dt * dt;
    out.speed = in.speed - gravity * dt;
    // Down to within a hair of it counts as landed (rounding).
    constexpr float landing_tolerance = 1.0e-4f;
    if (out.y <= surface + landing_tolerance) {
        if (out.speed <= 0.0f) {
            return vertical_body{surface, 0.0f, true};
        }
        out.y = surface;  // still rising, over ground rising under it
    }
    out.grounded = false;
    return out;
}
