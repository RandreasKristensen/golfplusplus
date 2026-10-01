#include "game/progression.h"

#include <algorithm>
#include <cmath>

namespace {
// Each level needs this much more XP than the last, so early levels come fast.
constexpr double xp_curve_base = 1.0885;

int clamp_xp(const int xp) {
    return std::clamp(xp, 0, skill_max_xp);
}
}

int xp_for_level(const int level) {
    const int clamped_level = std::clamp(level, 1, skill_max_level);
    if (clamped_level == 1) {
        return 0;
    }
    if (clamped_level == skill_max_level) {
        return skill_max_xp;
    }

    const double numerator = std::pow(xp_curve_base, static_cast<double>(clamped_level - 1)) - 1.0;
    const double denominator = std::pow(xp_curve_base, static_cast<double>(skill_max_level - 1)) - 1.0;
    return clamp_xp(static_cast<int>(std::floor(skill_max_xp * numerator / denominator + 0.5)));
}

int skill_level(const int xp) {
    const int clamped_xp = clamp_xp(xp);
    int level = 1;
    while (level < skill_max_level && clamped_xp >= xp_for_level(level + 1)) {
        ++level;
    }
    return level;
}

int skill_xp(const skill_progression& progression, const std::string& skill_id) {
    const auto it = progression.find(skill_id);
    return it == progression.end() ? 0 : clamp_xp(it->second.xp);
}

int xp_to_next_level(const skill_progression& progression, const std::string& skill_id) {
    const int xp = skill_xp(progression, skill_id);
    const int level = skill_level(xp);
    return level >= skill_max_level ? 0 : xp_for_level(level + 1) - xp;
}

add_skill_xp_result add_skill_xp(skill_progression& progression, const std::string& skill_id, const int amount) {
    add_skill_xp_result result;
    result.before_xp = skill_xp(progression, skill_id);
    result.after_xp = result.before_xp;
    if (skill_id.empty() || amount <= 0) {
        return result;
    }

    // before_xp <= skill_max_xp, so the subtraction never overflows.
    result.after_xp = amount > skill_max_xp - result.before_xp ? skill_max_xp : result.before_xp + amount;
    progression[skill_id].xp = result.after_xp;
    result.applied_xp = result.after_xp - result.before_xp;
    return result;
}
