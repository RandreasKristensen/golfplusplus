#pragma once

// RuneScape-style skills: any skill id maps to an XP total, and the level
// (1-99) follows from the XP curve. The skill list itself is data
// (assets/progression/skills.json).

#include <map>
#include <string>

inline constexpr int skill_max_level = 99;
inline constexpr int skill_max_xp = 1000000;

struct skill_progress {
    int xp = 0;
};

using skill_progression = std::map<std::string, skill_progress>;

struct add_skill_xp_result {
    int before_xp = 0;
    int after_xp = 0;
    int applied_xp = 0;  // after - before; less than asked for at the XP cap
};

// XP needed to reach `level` (clamped to 1..99).
int xp_for_level(int level);
int skill_level(int xp);
// 0 for skills with no XP yet.
int skill_xp(const skill_progression& progression, const std::string& skill_id);
int xp_to_next_level(const skill_progression& progression, const std::string& skill_id);
// Adds XP, clamped to [0, skill_max_xp]. Non-positive amounts change nothing.
add_skill_xp_result add_skill_xp(skill_progression& progression, const std::string& skill_id, int amount);
