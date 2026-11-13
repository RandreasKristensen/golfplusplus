#pragma once

// XP rewards as data (assets/play/progression/). Offline play and the server
// read the same file, so the numbers have one source. The skills they pay
// into are game/skill_catalog.h.

#include <optional>
#include <string>

inline constexpr const char* rewards_path = "play/progression/rewards.json";

struct xp_reward {
    std::string skill_id;
    int xp = 0;
};

struct movement_xp_rate {
    std::string skill_id;
    float meters_per_xp = 0.0f;
};

struct reward_rules {
    xp_reward dap; // each participant, once per accepted paired greeting
    xp_reward shot;  // per stroke taken
    movement_xp_rate walking;
    movement_xp_rate cart_on_road;
};

// Every field is required.
std::optional<reward_rules> parse_rewards_from_text(const std::string& text);
