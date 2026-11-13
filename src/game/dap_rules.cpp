#include "game/dap_rules.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"
#include <glm/geometric.hpp>
std::string dap_award_key(const std::uint64_t pair_id, const std::uint64_t golfer_id) {
    return "dap:" + std::to_string(pair_id) + ":" + std::to_string(golfer_id);
}
bool dap_running(const std::int64_t started_at, const std::int64_t now, const dap_tuning& tuning) {
    return now >= started_at && now - started_at < static_cast<std::int64_t>(tuning.seconds * 1000000.0f);
}
std::string check_dap(const dap_participant& first, const dap_participant& second, const game_tuning& tuning) {
    if (!first.account || !second.account || first.account == second.account) { return error_not_available; }
    if (!first.room || first.room != second.room) { return error_not_in_room; }
    const auto available = [](const dap_participant& p) {
        return !p.busy && (p.mode == motion_mode::idle || p.mode == motion_mode::walk);
    };
    if (!available(first) || !available(second)) { return error_not_available; }
    const float distance = glm::distance(first.position, second.position) * tuning.scale.meters_per_world_unit;
    if (!std::isfinite(distance) || distance > tuning.dap.reach_meters) { return error_too_far; }
    return {};
}
