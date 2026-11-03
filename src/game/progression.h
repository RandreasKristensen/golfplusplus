#pragma once

// RuneScape-style skills: any skill id maps to an XP total, and its level
// follows from its curve (game/skill_catalog.h). XP is never capped or thrown
// away: past a curve's top level it keeps counting, so a higher top later
// gives back everything earned.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct skill_progress {
    std::int64_t xp = 0;
};

using skill_progression = std::map<std::string, skill_progress>;

// The XP each level starts at: level_xp[n] reaches level n + 1, so
// level_xp[0] is 0, and its size is the top level. Integers, so every
// platform reads the same level.
struct level_curve {
    std::vector<std::int64_t> level_xp;
};

struct add_skill_xp_result {
    std::int64_t before_xp = 0;
    std::int64_t after_xp = 0;
    std::int64_t applied_xp = 0;
};

int level_at(const level_curve& curve, std::int64_t xp);
// 0 at the top level.
std::int64_t xp_to_next_level(const level_curve& curve, std::int64_t xp);
// 0 for skills with no XP yet.
std::int64_t skill_xp(const skill_progression& progression, const std::string& skill_id);
// Non-positive amounts change nothing.
add_skill_xp_result add_skill_xp(skill_progression& progression, const std::string& skill_id, std::int64_t amount);
