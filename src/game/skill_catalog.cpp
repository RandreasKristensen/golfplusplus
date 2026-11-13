#include "game/skill_catalog.h"

#include "game/json_util.h"

#include <algorithm>
#include <set>

namespace {
std::optional<std::map<std::string, level_curve>> curves_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    const json* curves = root ? json_object(*root, "curves") : nullptr;
    if (curves == nullptr) {
        return std::nullopt;
    }
    std::map<std::string, level_curve> result;
    for (auto it = curves->begin(); it != curves->end(); ++it) {
        level_curve curve;
        if (!it.value().is_array()) {
            return std::nullopt;
        }
        for (const json& xp : it.value()) {
            if (!xp.is_number_integer()) {
                return std::nullopt;
            }
            curve.level_xp.push_back(xp.get<std::int64_t>());
        }
        const bool rises = std::adjacent_find(curve.level_xp.begin(), curve.level_xp.end(),
                                              [](const std::int64_t a, const std::int64_t b) { return b <= a; }) ==
            curve.level_xp.end();
        if (curve.level_xp.empty() || curve.level_xp.front() != 0 || !rises) {
            return std::nullopt;
        }
        result[it.key()] = curve;
    }
    return result;
}

const level_curve* curve_of(const skill_catalog& catalog, const std::string& skill_id) {
    const skill_definition* skill = find_skill(catalog, skill_id);
    const auto curve = skill ? catalog.curves.find(skill->curve) : catalog.curves.end();
    return curve != catalog.curves.end() ? &curve->second : nullptr;
}
}

std::optional<skill_catalog> parse_skill_catalog_from_text(const std::string& skills_text, const std::string& curves_text) {
    const std::optional<json> root = parse_json(skills_text);
    const json* skills = root ? json_array(*root, "skills") : nullptr;
    std::optional<std::map<std::string, level_curve>> curves = curves_from_text(curves_text);
    if (skills == nullptr || !curves) {
        return std::nullopt;
    }

    skill_catalog catalog;
    catalog.curves = std::move(*curves);
    std::set<std::string> ids;
    for (const json& value : *skills) {
        const std::optional<std::string> id = json_string(value, "id");
        const std::optional<std::string> icon = json_string(value, "icon");
        const std::optional<std::string> curve = json_string(value, "curve");
        if (!id || id->empty() || !icon || !curve || catalog.curves.count(*curve) == 0 || !ids.insert(*id).second) {
            return std::nullopt;
        }
        catalog.skills.push_back(skill_definition{*id, *icon, *curve, json_bool(value, "retired").value_or(false)});
    }
    return catalog;
}

const skill_definition* find_skill(const skill_catalog& catalog, const std::string& skill_id) {
    const auto found = std::find_if(catalog.skills.begin(), catalog.skills.end(),
                                    [&skill_id](const skill_definition& skill) { return skill.id == skill_id; });
    return found != catalog.skills.end() ? &*found : nullptr;
}

int skill_level(const skill_catalog& catalog, const skill_progression& progression, const std::string& skill_id) {
    const level_curve* curve = curve_of(catalog, skill_id);
    return curve ? level_at(*curve, skill_xp(progression, skill_id)) : 1;
}

std::int64_t xp_to_next_level(const skill_catalog& catalog, const skill_progression& progression, const std::string& skill_id) {
    const level_curve* curve = curve_of(catalog, skill_id);
    return curve ? xp_to_next_level(*curve, skill_xp(progression, skill_id)) : 0;
}

std::map<std::string, int> skill_levels(const skill_catalog& catalog, const skill_progression& progression) {
    std::map<std::string, int> levels;
    for (const skill_definition& skill : catalog.skills) {
        levels[skill.id] = skill_level(catalog, progression, skill.id);
    }
    return levels;
}

std::string unpayable_skill(const skill_catalog& catalog, const reward_rules& rewards, const item_catalog& items) {
    std::vector<std::string> paid{rewards.shot.skill_id, rewards.walking.skill_id, rewards.cart_on_road.skill_id, rewards.dap.skill_id};
    for (const auto& [id, item] : items.items) {
        if (item.use && item.use->xp_per_use) {
            paid.push_back(item.use->xp_per_use->skill_id);
        }
    }
    for (const std::string& skill_id : paid) {
        const skill_definition* skill = find_skill(catalog, skill_id);
        if (skill == nullptr || skill->retired) {
            return skill_id;
        }
    }
    return {};
}
