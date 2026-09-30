#pragma once

#include <map>
#include <string>

constexpr int skill_max_level = 99;
constexpr int skill_max_xp = 1000000;

struct skill_progress {
    int xp = 0;
};

using skill_progression = std::map<std::string, skill_progress>;

struct add_skill_xp_result {
    int before_xp = 0;
    int after_xp = 0;
    int applied_xp = 0;
};

const char* golf_swing_skill_id();
const char* smoking_skill_id();
const char* fitness_skill_id();
const char* cart_driving_skill_id();
const char* drifting_skill_id();

skill_progression default_skill_progression();
int xp_for_level(int level);
int skill_level(int xp);
int skill_xp(const skill_progression& progression, const std::string& skill_id);
int xp_to_next_level(const skill_progression& progression, const std::string& skill_id);
void ensure_default_skills(skill_progression& progression);
add_skill_xp_result add_skill_xp(skill_progression& progression, const std::string& skill_id, int amount);
