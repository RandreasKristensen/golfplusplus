#include "game/effect_text.h"

#include "game/text_ids.h"

#include <cmath>
#include <cstdio>

namespace {
// The most seconds a square counts in seconds.
constexpr int most_seconds_shown = 99;
constexpr int seconds_per_minute = 60;

// `value` with its sign, whole when it is, else to two places without trailing zeros.
std::string signed_number(const float value) {
    char digits[32];
    if (std::abs(value - std::round(value)) < 0.005f) {
        std::snprintf(digits, sizeof digits, "%+d", static_cast<int>(std::lround(value)));
        return digits;
    }
    std::snprintf(digits, sizeof digits, "%+.2f", static_cast<double>(value));
    std::string number = digits;
    while (number.back() == '0') {
        number.pop_back();
    }
    return number;
}

const char* target_text_id(const std::string& target) {
    if (target == effect_target_walk_speed) {
        return text_effect_target_walk_speed;
    }
    if (target == effect_target_cart_top_speed) {
        return text_effect_target_cart_top_speed;
    }
    if (target == effect_target_cart_uphill) {
        return text_effect_target_cart_uphill;
    }
    if (target == effect_target_cart_drift) {
        return text_effect_target_cart_drift;
    }
    if (target == effect_target_shot_spread) {
        return text_effect_target_shot_spread;
    }
    if (target == effect_target_shot_club_speed) {
        return text_effect_target_shot_club_speed;
    }
    if (target == effect_target_all_xp) {
        return text_effect_target_all_xp;
    }
    return nullptr;
}
}

std::string effect_seconds_text(const text_assets& text, const int seconds) {
    if (seconds <= most_seconds_shown) {
        return std::to_string(seconds);
    }
    const int minutes = (seconds + seconds_per_minute - 1) / seconds_per_minute;
    return format_text(text, text_hud_effect_minutes, {{"minutes", std::to_string(minutes)}});
}

std::string effect_target_text(const text_assets& text, const std::string& target) {
    if (const char* id = target_text_id(target)) {
        return lookup_text(text, id);
    }
    const std::string prefix = effect_target_xp_prefix;
    if (target.rfind(prefix, 0) == 0) {
        return format_text(text, text_effect_target_skill_xp,
                           {{"skill", lookup_text(text, skill_text_key(target.substr(prefix.size())).c_str())}});
    }
    return lookup_text(text, ("effect.target." + target).c_str());
}

std::string effect_change_text(const text_assets& text, const item_effect& effect) {
    const std::string target = effect_target_text(text, effect.target);
    if (effect.bucket == effect_bucket::flat) {
        return format_text(text, text_effect_change_flat, {{"value", signed_number(effect.value)}, {"target", target}});
    }
    return format_text(text, text_effect_change_percent,
                       {{"value", signed_number(std::round(effect.value * 100.0f))}, {"target", target}});
}
