#include "game/hub_ball.h"
#include "physics/vector_math.h"
#include <glm/geometric.hpp>
#include <cmath>

std::optional<glm::vec3> practice_drop_position(const play_area& area, const place_kind kind, const glm::vec3& accepted,
    const glm::vec3& requested, const game_tuning& tuning) {
    if (kind != place_kind::hub) { return std::nullopt; }
    const float reach = tuning.server.warmup_reach_meters / tuning.scale.meters_per_world_unit;
    const bool near = std::isfinite(requested.x) && std::isfinite(requested.y) && std::isfinite(requested.z) &&
        glm::distance(accepted, requested) <= reach;
    return resting_ball_position(area, near ? requested : accepted, tuning.scale.ball_physics_radius_meters);
}

bool practice_pickup_in_reach(const ball_kind kind, const bool resting, const bool on_foot, const glm::vec3& accepted,
    const glm::vec3& ball, const game_tuning& tuning) {
    return kind == ball_kind::practice && resting && on_foot &&
        horizontal_distance(accepted, ball) <= tuning.player.ball_interact_radius &&
        std::abs(accepted.y - ball.y) <= tuning.player.ball_interact_radius;
}
