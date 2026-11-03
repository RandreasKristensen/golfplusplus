#include "game/shot_snapshot.h"

#include "game/cart_seats.h"

namespace {
solid stopping_ball(solid thing, const solid_contact_tuning& contact) {
    thing.stops_ball = true;
    thing.restitution = contact.restitution;
    thing.friction = contact.friction;
    return thing;
}

solid solid_of(const snapshot_entry& entry, const game_tuning& tuning) {
    const float unit = tuning.scale.meters_per_world_unit;
    switch (entry.kind) {
    case snapshot_kind::cart:
        return stopping_ball(cart_solid(cart_pose{entry.position, entry.yaw}, tuning.cart, tuning.rangefinder.cart_height),
                             tuning.solids.cart);
    case snapshot_kind::ball: {
        const float radius = tuning.scale.ball_physics_radius_meters / unit;
        solid ball;
        ball.kind = solid_kind::ball;
        ball.base = entry.position - glm::vec3(0.0f, radius, 0.0f);
        ball.radius = radius;
        ball.height = 2.0f * radius;
        return stopping_ball(ball, tuning.solids.ball);
    }
    case snapshot_kind::person:
        break;
    }
    solid person;
    person.kind = solid_kind::person;
    person.base = entry.position;
    person.radius = tuning.player.body_radius;
    person.height = tuning.rangefinder.person_height;
    return stopping_ball(person, tuning.solids.person);
}
}

std::vector<solid> snapshot_solids(const std::vector<snapshot_entry>& entries, const game_tuning& tuning) {
    std::vector<solid> solids;
    for (const snapshot_entry& entry : entries) {
        if (!entry.shooter) {
            solids.push_back(solid_of(entry, tuning));
        }
    }
    for (const snapshot_entry& entry : entries) {
        if (entry.shooter) {
            solids.push_back(solid_of(entry, tuning));
        }
    }
    return solids;
}
