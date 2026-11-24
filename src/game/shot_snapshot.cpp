#include "game/shot_snapshot.h"

#include "game/cart_seats.h"
#include "game/fallen_body.h"

namespace {
solid stopping_ball(solid thing, const solid_contact_tuning& contact) {
    thing.stops_ball = true;
    thing.restitution = contact.restitution;
    thing.friction = contact.friction;
    return thing;
}
}

solid snapshot_solid(const snapshot_entry& entry, const game_tuning& tuning) {
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
    case snapshot_kind::body: {
        fallen_body lying;
        lying.position = entry.position;
        lying.yaw = entry.yaw;
        return body_solid(lying, tuning);
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

namespace {
// The entries in solid order: everyone but the shooter, then the shooter.
std::vector<std::size_t> solid_order(const std::vector<snapshot_entry>& entries) {
    std::vector<std::size_t> order;
    for (const bool shooter : {false, true}) {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            if (entries[i].shooter == shooter) {
                order.push_back(i);
            }
        }
    }
    return order;
}
}

std::vector<solid> snapshot_solids(const std::vector<snapshot_entry>& entries, const game_tuning& tuning) {
    std::vector<solid> solids;
    for (const std::size_t i : solid_order(entries)) {
        solids.push_back(snapshot_solid(entries[i], tuning));
    }
    return solids;
}

snapshot_split split_snapshot(const std::vector<snapshot_entry>& entries) {
    snapshot_split split;
    for (const snapshot_entry& entry : entries) {
        const bool moves = entry.kind == snapshot_kind::person || entry.kind == snapshot_kind::cart;
        (moves ? split.movers : split.still).push_back(entry);
    }
    return split;
}

std::optional<std::size_t> snapshot_entry_of_solid(const std::vector<snapshot_entry>& entries, const std::size_t solid_index) {
    const std::vector<std::size_t> order = solid_order(entries);
    if (solid_index >= order.size()) {
        return std::nullopt;
    }
    return order[solid_index];
}
