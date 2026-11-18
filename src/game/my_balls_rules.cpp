#include "game/my_balls.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

std::optional<glm::vec3> practice_drop_position(const play_area& area, const place_rules& rules, const glm::vec3& accepted,
    const glm::vec3& requested, const game_tuning& tuning) {
    if (!rules.drops) { return std::nullopt; }
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

std::vector<spare_ball> spares_after_new_ball(std::vector<spare_ball> spares, const std::optional<spare_ball>& demoted,
    const int balls_out) {
    if (demoted) { spares.push_back(*demoted); }
    std::stable_sort(spares.begin(), spares.end(), [](const spare_ball& a, const spare_ball& b) {
        return a.placed_at != b.placed_at ? a.placed_at < b.placed_at : a.id < b.id;
    });
    const std::size_t room = static_cast<std::size_t>(std::max(balls_out - 1, 0));
    if (spares.size() > room) { spares.erase(spares.begin(), spares.end() - static_cast<std::ptrdiff_t>(room)); }
    return spares;
}

std::vector<snapshot_entry> spares_in_snapshot(const std::vector<spare_ball>& spares, const glm::vec3& struck_from,
    const game_tuning& tuning) {
    const float diameter = 2.0f * tuning.scale.ball_physics_radius_meters / tuning.scale.meters_per_world_unit;
    std::vector<snapshot_entry> entries;
    for (const spare_ball& spare : spares) {
        if (!spare.in_cup && glm::distance(spare.position, struck_from) >= diameter) {
            entries.push_back(snapshot_entry{snapshot_kind::ball, spare.position, 0.0f, false});
        }
    }
    return entries;
}

std::string play_spare_error(const std::optional<ball_in_play_state>& playing, const bool on_foot, const glm::vec3& accepted,
    const spare_ball& spare, const game_tuning& tuning) {
    if (spare.in_cup) { return error_ball_in_cup; }
    if (!practice_pickup_in_reach(ball_kind::practice, true, on_foot, accepted, spare.position, tuning)) { return error_too_far; }
    if (playing && (playing->kind != ball_kind::practice || !playing->resting)) { return error_ball_in_play; }
    return {};
}

std::optional<lift_choice> nearest_ball_to_lift(const std::optional<glm::vec3>& in_play, const std::vector<spare_ball>& spares,
    const bool on_foot, const glm::vec3& accepted, const game_tuning& tuning) {
    std::optional<lift_choice> best;
    float best_distance = 0.0f;
    const auto consider = [&](const lift_choice& choice, const glm::vec3& at) {
        if (!practice_pickup_in_reach(ball_kind::practice, true, on_foot, accepted, at, tuning)) { return; }
        const float distance = horizontal_distance(accepted, at);
        if (!best || distance < best_distance) { best = choice; best_distance = distance; }
    };
    std::vector<spare_ball> oldest_first = spares;
    std::stable_sort(oldest_first.begin(), oldest_first.end(), [](const spare_ball& a, const spare_ball& b) {
        return a.placed_at != b.placed_at ? a.placed_at < b.placed_at : a.id < b.id;
    });
    for (const spare_ball& spare : oldest_first) { consider(lift_choice{false, spare.id}, spare.position); }
    if (in_play) { consider(lift_choice{true, 0}, *in_play); }
    return best;
}
