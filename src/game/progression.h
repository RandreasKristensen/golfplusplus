#pragma once

#include <map>
#include <string>
#include <vector>

constexpr int skill_max_level = 99;
constexpr int skill_max_xp = 1000000;

struct skill_progress {
    int xp = 0;
};

using skill_progression = std::map<std::string, skill_progress>;

const char* golf_swing_skill_id();
const char* smoking_skill_id();
const char* fitness_skill_id();
const char* cart_driving_skill_id();
const char* drifting_skill_id();
const char* rangefinder_unlock_id();
const char* cart_unlock_id();
const char* cigarette_filterless_unlock_id();
const char* cigarette_menthol_unlock_id();
const char* cigarette_longcut_unlock_id();

skill_progression default_skill_progression();
bool has_unlock(const std::vector<std::string>& unlocked_items, const std::string& unlock_id);
int xp_for_level(int level);
int skill_level(int xp);
int skill_xp(const skill_progression& progression, const std::string& skill_id);
int xp_to_next_level(const skill_progression& progression, const std::string& skill_id);
void ensure_default_skills(skill_progression& progression);
void add_skill_xp(skill_progression& progression, const std::string& skill_id, int amount);
