#include "game/lightning_rules.h"

#include "game/shot_dice.h"
#include "game/weather.h"
#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <cmath>
#include <optional>

#include <glm/geometric.hpp>

namespace {
// The dice stream of strike rolls (the flight's steps count up from 0, the
// swing's is -1, the hits' -2).
constexpr int strike_dice_step = -3;
constexpr double seconds_per_hour = 3600.0;
constexpr float small_exponent = 0.01f;

// Whether `thing` is over the XZ point `feet` with its underside at `head` or higher.
bool over_head(const solid& thing, const glm::vec3& feet, const float head) {
    if (thing.base.y < head) {
        return false;
    }
    switch (thing.shape) {
    case solid_shape::cylinder:
    case solid_shape::cone:
        return horizontal_distance(thing.base, feet) <= thing.radius;
    case solid_shape::box: {
        const glm::vec3 offset = horizontal(feet - thing.base);
        const glm::vec3 across(-thing.along.z, 0.0f, thing.along.x);
        return std::abs(glm::dot(offset, thing.along)) <= thing.half_length &&
            std::abs(glm::dot(offset, across)) <= thing.half_width;
    }
    case solid_shape::wall:
    case solid_shape::slab:
        break;
    }
    return false;
}

std::optional<lightning_cover> cover_of(const solid& thing) {
    if (thing.kind == solid_kind::leaves) {
        return lightning_cover::tree;
    }
    if (thing.kind == solid_kind::board && thing.shape == solid_shape::box) {
        return lightning_cover::building;
    }
    return std::nullopt;
}

float cover_factor(const lightning_cover cover, const lightning_tuning& tuning) {
    switch (cover) {
    case lightning_cover::tree: return tuning.tree_factor;
    case lightning_cover::building: return tuning.building_factor;
    case lightning_cover::open: break;
    }
    return 1.0f;
}
}

float storm_factor(const float thunder) {
    return clamp01((thunder - thunderstorm_thunder) / (1.0f - thunderstorm_thunder));
}

lightning_cover cover_over(const std::vector<solid>& solids, const solid_grid& grid, const glm::vec3& feet,
                           const game_tuning& tuning) {
    const float head = feet.y + tuning.rangefinder.person_height;
    lightning_cover cover = lightning_cover::open;
    float lowest = 0.0f;
    bool found = false;
    for (const std::uint32_t index : solids_near(grid, feet, feet, 0.0f)) {
        const solid& thing = solids[index];
        const std::optional<lightning_cover> kind = cover_of(thing);
        if (kind && over_head(thing, feet, head) && (!found || thing.base.y < lowest)) {
            cover = *kind;
            lowest = thing.base.y;
            found = true;
        }
    }
    return cover;
}

float strike_rate_per_second(const float thunder, const lightning_cover cover, const bool club_in_hand,
                             const lightning_tuning& tuning) {
    const float storm = storm_factor(thunder);
    if (storm <= 0.0f) {
        return 0.0f;
    }
    const double open = 1.0 / (static_cast<double>(tuning.open_strike_hours) * seconds_per_hour);
    return static_cast<float>(open * static_cast<double>(storm) * static_cast<double>(cover_factor(cover, tuning)) *
                              static_cast<double>(club_in_hand ? tuning.address_factor : 1.0f));
}

float strike_chance(const float rate_per_second, const float seconds) {
    const float x = rate_per_second * seconds;
    // The series keeps the small chances a float cannot take from 1 - exp(-x).
    return x < small_exponent ? x * (1.0f - 0.5f * x * (1.0f - x / 3.0f)) : 1.0f - det_exp(-x);
}

std::int64_t lightning_check_at(const std::int64_t unix_ms, const lightning_tuning& tuning) {
    const std::int64_t length = static_cast<std::int64_t>(std::llround(static_cast<double>(tuning.check_seconds) * 1000.0));
    const std::int64_t whole = unix_ms / length;
    return unix_ms % length < 0 ? whole - 1 : whole;
}

std::uint64_t lightning_check_seed(const std::uint64_t seed, const std::int64_t check) {
    return round_shot_seed(seed ^ (static_cast<std::uint64_t>(check) * 0xD6E8FEB86659FD93ull), 0);
}

std::vector<lightning_strike_hit> lightning_strikes(const std::vector<lightning_golfer>& golfers, const float thunder,
                                                    const std::uint64_t check_seed, const std::vector<solid>& solids,
                                                    const solid_grid& grid, const game_tuning& tuning) {
    std::vector<lightning_strike_hit> hits;
    if (storm_factor(thunder) <= 0.0f) {
        return hits;
    }
    const lightning_tuning& numbers = tuning.lightning;
    for (const lightning_golfer& golfer : golfers) {
        const std::uint64_t seed = round_shot_seed(check_seed + golfer.account * 0x9E3779B97F4A7C15ull, 0);
        const float rate = strike_rate_per_second(thunder, cover_over(solids, grid, golfer.position, tuning),
                                                  golfer.club_in_hand, numbers);
        if (dice_uniform(seed, strike_dice_step, 0) >= strike_chance(rate, numbers.check_seconds)) {
            continue;
        }
        hits.push_back(lightning_strike_hit{golfer.account, golfer.position,
                                            dice_uniform(seed, strike_dice_step, 1) < numbers.knockout_share
                                                ? lightning_outcome::knocked_out
                                                : lightning_outcome::dazed});
    }
    return hits;
}
