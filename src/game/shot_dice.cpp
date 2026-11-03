#include "game/shot_dice.h"

namespace {
std::uint64_t mixed(std::uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

constexpr int normal_draws = 12;
}

float dice_uniform(const std::uint64_t seed, const int step, const std::uint64_t draw) {
    const std::uint64_t x = seed ^ (static_cast<std::uint64_t>(static_cast<std::int64_t>(step)) * 0x9E3779B97F4A7C15ull) ^
        (draw * 0xBF58476D1CE4E5B9ull);
    return static_cast<float>(mixed(x) >> 40) / static_cast<float>(1ull << 24);
}

float dice_normal(const std::uint64_t seed, const int step, const std::uint64_t first) {
    float sum = 0.0f;
    for (int i = 0; i < normal_draws; ++i) {
        sum += dice_uniform(seed, step, first + static_cast<std::uint64_t>(i));
    }
    return sum - 0.5f * static_cast<float>(normal_draws);
}

swing_errors rolled_errors(const std::uint64_t seed, const swing_errors& spread) {
    const auto roll = [seed](const int channel) {
        return dice_normal(seed, shot_strike_step, static_cast<std::uint64_t>(channel * normal_draws));
    };
    swing_errors out;
    out.speed_share = spread.speed_share * roll(0);
    out.face = spread.face * roll(1);
    out.path = spread.path * roll(2);
    out.strike_toe = spread.strike_toe * roll(3);
    out.strike_high = spread.strike_high * roll(4);
    return out;
}

std::uint64_t round_shot_seed(const std::uint64_t round_seed, const int count) {
    return mixed(round_seed + static_cast<std::uint64_t>(count) * 0x9E3779B97F4A7C15ull);
}
