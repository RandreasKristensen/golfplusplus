#pragma once

// Every skill there is and the curve its levels follow, as data
// (assets/play/progression/skills.json, curves.json). Skill ids are forever:
// a skill no longer earned stays listed (`retired`), so progress in it is
// kept and shown. Offline play and the server read the same files.

#include "game/item_catalog.h"
#include "game/progression.h"
#include "game/reward_rules.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* skills_path = "play/progression/skills.json";
inline constexpr const char* curves_path = "play/progression/curves.json";

struct skill_definition {
    std::string id;
    std::string icon;   // XP drop icon name; see skill_icon_from_name in renderer/render_data.h
    std::string curve;  // a curve in skill_catalog::curves
    bool retired = false;  // earns nothing any more; its progress stays
};

struct skill_catalog {
    std::vector<skill_definition> skills;  // skills panel order
    std::map<std::string, level_curve> curves;
};

// nullopt on a malformed entry, a repeated id, a skill whose curve is not in
// `curves_text`, or a curve that does not start at 0 and rise every level.
std::optional<skill_catalog> parse_skill_catalog_from_text(const std::string& skills_text, const std::string& curves_text);

const skill_definition* find_skill(const skill_catalog& catalog, const std::string& skill_id);
// 1 for a skill the catalog does not list.
int skill_level(const skill_catalog& catalog, const skill_progression& progression, const std::string& skill_id);
std::int64_t xp_to_next_level(const skill_catalog& catalog, const skill_progression& progression, const std::string& skill_id);
// Every listed skill's level: a golfer's (golfer_profile::levels).
std::map<std::string, int> skill_levels(const skill_catalog& catalog, const skill_progression& progression);

// The first skill `rewards` or an item's use pays into that the catalog
// does not list or has retired; empty when there is none.
std::string unpayable_skill(const skill_catalog& catalog, const reward_rules& rewards, const item_catalog& items);
