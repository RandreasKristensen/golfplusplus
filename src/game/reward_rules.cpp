#include "game/reward_rules.h"

#include "game/json_util.h"

namespace {
std::optional<xp_reward> xp_reward_from_json(const json& root, const char* key) {
    const json* object = json_object(root, key);
    const std::optional<std::string> skill = object != nullptr ? json_string(*object, "skill") : std::nullopt;
    const std::optional<int> xp = object != nullptr ? json_int(*object, "xp") : std::nullopt;
    if (!skill || !xp || *xp <= 0) {
        return std::nullopt;
    }
    return xp_reward{*skill, *xp};
}

std::optional<movement_xp_rate> movement_rate_from_json(const json& movement, const char* key) {
    const json* object = json_object(movement, key);
    const std::optional<std::string> skill = object != nullptr ? json_string(*object, "skill") : std::nullopt;
    const std::optional<float> meters = object != nullptr ? json_float(*object, "meters_per_xp") : std::nullopt;
    if (!skill || !meters || *meters <= 0.0f) {
        return std::nullopt;
    }
    return movement_xp_rate{*skill, *meters};
}
}

std::optional<std::vector<skill_definition>> parse_skills_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    const json* skills = root ? json_array(*root, "skills") : nullptr;
    if (skills == nullptr) {
        return std::nullopt;
    }

    std::vector<skill_definition> result;
    for (const json& value : *skills) {
        const std::optional<std::string> id = json_string(value, "id");
        const std::optional<std::string> icon = json_string(value, "icon");
        if (!id || id->empty() || !icon) {
            return std::nullopt;
        }
        result.push_back(skill_definition{*id, *icon});
    }
    return result;
}

std::optional<reward_rules> parse_rewards_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    const json* movement = root ? json_object(*root, "movement_xp") : nullptr;
    const json* cigarette = root ? json_object(*root, "cigarette") : nullptr;
    if (movement == nullptr || cigarette == nullptr) {
        return std::nullopt;
    }

    const std::optional<xp_reward> shot = xp_reward_from_json(*root, "shot_xp");
    const std::optional<xp_reward> smoke = xp_reward_from_json(*root, "smoke_xp");
    const std::optional<movement_xp_rate> walking = movement_rate_from_json(*movement, "walking");
    const std::optional<movement_xp_rate> cart = movement_rate_from_json(*movement, "cart_on_road");
    const std::optional<movement_xp_rate> drift = movement_rate_from_json(*movement, "drift_on_road");
    const std::optional<float> duration = json_float(*cigarette, "duration_seconds");
    const std::optional<float> backspin = json_float(*cigarette, "backspin_scale");
    const std::optional<float> timing = json_float(*cigarette, "timing_speed_scale");
    if (!shot || !smoke || !walking || !cart || !drift || !duration || !backspin || !timing) {
        return std::nullopt;
    }

    reward_rules rules;
    rules.shot = *shot;
    rules.smoke = *smoke;
    rules.walking = *walking;
    rules.cart_on_road = *cart;
    rules.drift_on_road = *drift;
    rules.cigarette = cigarette_rules{*duration, *backspin, *timing};
    return rules;
}
