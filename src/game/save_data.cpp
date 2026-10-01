#include "game/save_data.h"

#include "game/json_util.h"

#include <algorithm>

namespace {
skill_progression skills_from_json(const json& root) {
    skill_progression progression;
    if (const json* skills = json_object(root, "skills")) {
        for (auto it = skills->begin(); it != skills->end(); ++it) {
            if (it.value().is_number_integer()) {
                progression[it.key()].xp = std::clamp(it.value().get<int>(), 0, skill_max_xp);
            }
        }
    }
    return progression;
}

std::map<std::string, repeatable_collectible_state> repeatable_collectibles_from_json(const json& root) {
    std::map<std::string, repeatable_collectible_state> values;
    if (const json* collectibles = json_object(root, "repeatable_collectibles")) {
        for (auto it = collectibles->begin(); it != collectibles->end(); ++it) {
            repeatable_collectible_state state;
            state.claim_count = std::max(0, json_int(it.value(), "claim_count").value_or(0));
            state.claimed_at_holes_completed = json_int(it.value(), "claimed_at_holes_completed");
            values[it.key()] = state;
        }
    }
    return values;
}
}

save_data migrate_save_data(save_data save) {
    // v1-v4: fields since removed (money, unlocks, quests, current course and
    // hole, hole scores). Parsing ignores them, so nothing to convert.
    if (save.version < 6) {
        // v6 counts completed holes for cooldowns; old claim positions were
        // per-round hole indices and are dropped.
        save.holes_completed = 0;
        for (auto& [id, state] : save.repeatable_collectibles) {
            state.claimed_at_holes_completed.reset();
        }
    }
    save.version = current_save_version;
    return save;
}

std::optional<save_data> parse_save_data(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return std::nullopt;
    }

    save_data save;
    save.version = json_int(*root, "version").value_or(0);
    if (save.version > current_save_version) {
        return std::nullopt;
    }
    save.completed_course_ids = json_string_array(*root, "completed_course_ids");
    save.holes_completed = std::max(0, json_int(*root, "holes_completed").value_or(0));
    save.skills = skills_from_json(*root);
    save.collected_ids = json_string_array(*root, "collected_ids");
    save.repeatable_collectibles = repeatable_collectibles_from_json(*root);
    save.world_flags = json_string_array(*root, "world_flags");
    return migrate_save_data(save);
}

std::string save_data_to_json(const save_data& save) {
    json root = json::object();
    root["version"] = current_save_version;
    root["completed_course_ids"] = save.completed_course_ids;
    root["holes_completed"] = save.holes_completed;
    root["collected_ids"] = save.collected_ids;
    root["world_flags"] = save.world_flags;

    json skills = json::object();
    for (const auto& [id, progress] : save.skills) {
        skills[id] = progress.xp;
    }
    root["skills"] = skills;

    json repeatable = json::object();
    for (const auto& [id, state] : save.repeatable_collectibles) {
        json value = json::object();
        value["claim_count"] = state.claim_count;
        if (state.claimed_at_holes_completed) {
            value["claimed_at_holes_completed"] = *state.claimed_at_holes_completed;
        }
        repeatable[id] = value;
    }
    root["repeatable_collectibles"] = repeatable;

    return root.dump(2) + "\n";
}
