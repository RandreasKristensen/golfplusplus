#include "game/progression.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr double xp_curve_base = 1.0885;

int clamp_xp(const int xp) {
    return std::max(0, std::min(skill_max_xp, xp));
}
}

const char* golf_swing_skill_id() {
    return "golf_swing";
}

const char* smoking_skill_id() {
    return "smoking";
}

const char* fitness_skill_id() {
    return "fitness";
}

const char* rangefinder_unlock_id() {
    return "rangefinder";
}

const char* cart_unlock_id() {
    return "cart";
}

const char* cigarette_filterless_unlock_id() {
    return "cigarette_filterless";
}

const char* cigarette_menthol_unlock_id() {
    return "cigarette_menthol";
}

const char* cigarette_longcut_unlock_id() {
    return "cigarette_longcut";
}

skill_progression default_skill_progression() {
    skill_progression progression;
    progression[golf_swing_skill_id()] = skill_progress{};
    progression[smoking_skill_id()] = skill_progress{};
    progression[fitness_skill_id()] = skill_progress{};
    return progression;
}

bool has_unlock(const std::vector<std::string>& unlocked_items, const std::string& unlock_id) {
    return std::find(unlocked_items.begin(), unlocked_items.end(), unlock_id) != unlocked_items.end();
}

int xp_for_level(const int level) {
    const int clamped_level = std::max(1, std::min(skill_max_level, level));
    if (clamped_level <= 1) {
        return 0;
    }
    if (clamped_level >= skill_max_level) {
        return skill_max_xp;
    }

    const double numerator = std::pow(xp_curve_base, static_cast<double>(clamped_level - 1)) - 1.0;
    const double denominator = std::pow(xp_curve_base, static_cast<double>(skill_max_level - 1)) - 1.0;
    return clamp_xp(static_cast<int>(std::floor(skill_max_xp * numerator / denominator + 0.5)));
}

int skill_level(const int xp) {
    const int clamped_xp = clamp_xp(xp);
    int level = 1;
    for (int candidate = 2; candidate <= skill_max_level; ++candidate) {
        if (clamped_xp < xp_for_level(candidate)) {
            break;
        }
        level = candidate;
    }
    return level;
}

int skill_xp(const skill_progression& progression, const std::string& skill_id) {
    const auto it = progression.find(skill_id);
    if (it == progression.end()) {
        return 0;
    }
    return clamp_xp(it->second.xp);
}

int xp_to_next_level(const skill_progression& progression, const std::string& skill_id) {
    const int xp = skill_xp(progression, skill_id);
    const int level = skill_level(xp);
    if (level >= skill_max_level) {
        return 0;
    }
    return std::max(0, xp_for_level(level + 1) - xp);
}

void ensure_default_skills(skill_progression& progression) {
    for (const auto& default_skill : default_skill_progression()) {
        if (progression.find(default_skill.first) == progression.end()) {
            progression[default_skill.first] = default_skill.second;
        }
    }

    for (auto& skill : progression) {
        skill.second.xp = clamp_xp(skill.second.xp);
    }
}

void add_skill_xp(skill_progression& progression, const std::string& skill_id, const int amount) {
    if (amount <= 0 || skill_id.empty()) {
        return;
    }

    skill_progress& progress = progression[skill_id];
    if (progress.xp > skill_max_xp - amount) {
        progress.xp = skill_max_xp;
    } else {
        progress.xp = clamp_xp(progress.xp + amount);
    }
}
