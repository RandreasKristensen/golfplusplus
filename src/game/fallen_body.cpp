#include "game/fallen_body.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

fallen_body body_fallen_at(fallen_body body, const std::int64_t fell_at_ms, const knockout_tuning& tuning) {
    body.fell_at_ms = fell_at_ms;
    body.expires_at_ms = fell_at_ms + static_cast<std::int64_t>(std::llround(static_cast<double>(tuning.body_minutes) * 60000.0));
    return body;
}

bool body_expired(const fallen_body& body, const std::int64_t now_ms) {
    return now_ms >= body.expires_at_ms;
}

std::vector<fallen_body> bodies_lying(std::vector<fallen_body> bodies, const std::int64_t now_ms) {
    bodies.erase(std::remove_if(bodies.begin(), bodies.end(), [now_ms](const fallen_body& body) { return body_expired(body, now_ms); }),
                 bodies.end());
    return bodies;
}

solid body_solid(const fallen_body& body, const game_tuning& tuning) {
    const float unit = tuning.scale.meters_per_world_unit;
    solid lying;
    lying.shape = solid_shape::box;
    lying.kind = solid_kind::body;
    lying.base = body.position;
    lying.along = yaw_direction(body.yaw);
    lying.half_length = 0.5f * tuning.rangefinder.person_height;
    lying.half_width = tuning.knockout.body_half_width_meters / unit;
    lying.height = tuning.knockout.body_height_meters / unit;
    lying.stops_ball = true;
    lying.stops_walker = true;
    lying.stops_cart = true;
    lying.restitution = tuning.solids.person.restitution;
    lying.friction = tuning.solids.person.friction;
    return lying;
}

std::vector<solid> body_solids(const std::vector<fallen_body>& bodies, const game_tuning& tuning) {
    std::vector<solid> solids;
    for (const fallen_body& body : bodies) {
        solids.push_back(body_solid(body, tuning));
    }
    return solids;
}

snapshot_entry body_snapshot_entry(const fallen_body& body) {
    return snapshot_entry{snapshot_kind::body, body.position, body.yaw, false};
}
