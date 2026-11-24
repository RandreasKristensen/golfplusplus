#include "lightning_check_rules.h"

#include "game/server_errors.h"
#include "game/weather.h"

std::vector<room_lightning_hit> check_lightning_rooms(const std::vector<lightning_room_ref>& rooms,
                                                      const std::function<std::vector<lightning_golfer>(std::uint64_t)>& golfers_in,
                                                      const server_content& content, const std::int64_t now_ms,
                                                      const std::uint64_t seed) {
    const game_tuning& tuning = content.tuning;
    const std::uint64_t check_seed = lightning_check_seed(seed, lightning_check_at(now_ms, tuning.lightning));
    std::vector<room_lightning_hit> hits;
    for (const lightning_room_ref& room : rooms) {
        if (room.course == nullptr) {
            continue;
        }
        const float thunder = weather_at(tuning.weather, tuning.wind, tuning.sky_clock, room.course->course, now_ms).thunder;
        if (storm_factor(thunder) <= 0.0f) {
            continue;
        }
        for (const lightning_strike_hit& hit : lightning_strikes(golfers_in(room.room_id), thunder, check_seed,
                                                                 room.course->area.solids, room.course->area.solids_grid, tuning)) {
            hits.push_back(room_lightning_hit{room.room_id, hit});
        }
    }
    return hits;
}

room_lightning_hit god_strike_hit(const std::uint64_t room_id, const std::uint64_t account, const glm::vec3& position,
                                  const bool knock_out) {
    return room_lightning_hit{room_id, lightning_strike_hit{account, position,
                                                            knock_out ? lightning_outcome::knocked_out : lightning_outcome::dazed}};
}

std::string god_command_refusal(const bool allow_god) {
    return allow_god ? std::string() : std::string(error_god_disabled);
}
