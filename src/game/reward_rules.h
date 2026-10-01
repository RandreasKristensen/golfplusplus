#pragma once

// Skills and XP rewards as data (assets/progression/). Offline play and,
// later, the server read the same files, so the numbers have one source.

#include <optional>
#include <string>
#include <vector>

inline constexpr const char* skills_path = "progression/skills.json";
inline constexpr const char* rewards_path = "progression/rewards.json";

struct skill_definition {
    std::string id;
    std::string icon;  // XP drop icon name; see skill_icon_from_name in renderer.h
};

struct xp_reward {
    std::string skill_id;
    int xp = 0;
};

struct movement_xp_rate {
    std::string skill_id;
    float meters_per_xp = 0.0f;
};

struct cigarette_rules {
    float duration_seconds = 0.0f;
    float backspin_scale = 0.0f;
    float timing_speed_scale = 0.0f;
};

struct reward_rules {
    xp_reward shot;   // per stroke taken
    xp_reward smoke;  // per cigarette
    movement_xp_rate walking;
    movement_xp_rate cart_on_road;
    movement_xp_rate drift_on_road;
    cigarette_rules cigarette;
};

// Skills in skills-panel order. nullopt on any malformed entry.
std::optional<std::vector<skill_definition>> parse_skills_from_text(const std::string& text);
// Every field is required.
std::optional<reward_rules> parse_rewards_from_text(const std::string& text);
