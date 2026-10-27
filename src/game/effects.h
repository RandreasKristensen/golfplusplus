#pragma once

// What items do to the game's numbers (game/item_catalog.h): each effect
// adds to one target's flat, increased or more bucket while it lasts, and a
// final value is (base + sum of flat) x (1 + sum of increased) x the product
// of (1 + each more). Gameplay reads every number an item can change through
// effective_value, never an item's raw numbers. Times are UTC milliseconds
// on the shared clock (shared_unix_ms in game/mode_dispatch.h), never frame
// time, so the server works out the same values. Pure.

#include "game/reward_rules.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

// The targets gameplay reads. XP targets are "xp." and a skill id, or
// effect_target_all_xp for every skill.
inline constexpr const char* effect_target_xp_prefix = "xp.";
inline constexpr const char* effect_target_all_xp = "xp.all";
inline constexpr const char* effect_target_walk_speed = "player.walk_speed";
inline constexpr const char* effect_target_cart_top_speed = "cart.top_speed";
// The cart's motor force (climbing) and its tyres' grip (physics/cart_dynamics.h).
inline constexpr const char* effect_target_cart_uphill = "cart.uphill";
inline constexpr const char* effect_target_cart_drift = "cart.drift";
// Every spread of my swing (game/shot_ability.h): below 1 straighter.
inline constexpr const char* effect_target_shot_spread = "shot.spread";

enum class effect_bucket {
    flat,
    increased,
    more
};

enum class effect_duration {
    instant,   // once, as the item is used: nothing that lasts
    seconds,   // for `seconds` from the use
    use,       // while the use plays (the item's seconds_per_use)
    equipped   // while the item is equipped
};

struct item_effect {
    std::string target;
    effect_bucket bucket = effect_bucket::increased;
    float value = 0.0f;
    effect_duration duration = effect_duration::instant;
    float seconds = 0.0f;  // seconds only
};

inline constexpr std::int64_t effect_never_ends = std::numeric_limits<std::int64_t>::max();

// One effect running: from the use (or equipping) of `item`, over
// [started_at_ms, ends_at_ms).
struct active_effect {
    std::string item;
    item_effect effect;
    std::int64_t started_at_ms = 0;
    std::int64_t ends_at_ms = 0;
};

bool effect_running(const active_effect& effect, std::int64_t now_ms);

// `base` changed by the running effects on any of `targets` at `now_ms`.
float effective_value(float base, const std::vector<std::string>& targets, const std::vector<active_effect>& effects,
                      std::int64_t now_ms);
float effective_value(float base, const std::string& target, const std::vector<active_effect>& effects, std::int64_t now_ms);

// `reward` as its XP and every skill's XP targets have it, rounded to whole XP.
xp_reward effective_reward(const xp_reward& reward, const std::vector<active_effect>& effects, std::int64_t now_ms);

// `effects` without those that have ended by `now_ms`.
std::vector<active_effect> running_effects(std::vector<active_effect> effects, std::int64_t now_ms);
