#include "tee_rules.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"

bool tee_expired(const std::int64_t scheduled, const std::int64_t now, const bool started,
                  const server_tuning& tuning) {
    return !started && now >= scheduled + tuning.tee_expiry_minutes * minute_micros;
}
bool tee_missed(const std::int64_t scheduled, const std::int64_t now, const bool started, const bool boarded,
                const server_tuning& tuning) {
    return tee_expired(scheduled, now, started, tuning) || (!started && !boarded && now >= scheduled);
}
std::string check_booking(const std::uint64_t expected, const std::uint64_t current,
                          const std::int64_t scheduled, const std::int64_t now, const int occupied,
                          const bool same_slot, const server_tuning& tuning) {
    if (expected != current) { return error_stale_booking; }
    const auto interval = tuning.tee_interval_minutes * minute_micros;
    if (scheduled < now || scheduled % interval != 0 ||
        scheduled > now + tuning.tee_book_days * 24 * 60 * minute_micros) { return error_invalid_slot; }
    if (!same_slot && occupied >= tuning.group_capacity) { return error_group_full; }
    return {};
}
std::string check_bus(const std::int64_t scheduled, const std::int64_t now, const bool started,
                      const server_tuning& tuning) {
    if (tee_missed(scheduled, now, started, false, tuning)) { return error_booking_expired; }
    return now < scheduled - tuning.bus_early_minutes * minute_micros ? error_bus_too_early : "";
}
std::string check_first_tee(const std::int64_t scheduled, const std::int64_t now, const int hole,
                            const bool started, const bool tee_clear, const server_tuning& tuning) {
    if (started) { return error_stale_booking; }
    if (hole != 0) { return error_first_tee_required; }
    if (tee_expired(scheduled, now, false, tuning)) { return error_booking_expired; }
    return now < scheduled && !tee_clear ? error_tee_too_early : "";
}

glm::vec3 booked_tee_position(const glm::vec3& tee, const glm::vec3& pin, const std::uint32_t lane,
                               const game_tuning& tuning) {
    const glm::vec3 forward = safe_normalize(horizontal(pin - tee), glm::vec3(0.0f, 0.0f, 1.0f));
    return tee + yaw_left(forward) * (static_cast<float>(lane) * tuning.server.group_tee_spacing_meters /
                                    tuning.scale.meters_per_world_unit);
}
