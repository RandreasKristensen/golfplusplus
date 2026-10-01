#pragma once

// The offline save (one JSON file per slot). Online progress never goes here.
// Every change to this shape bumps current_save_version and adds a step to
// migrate_save_data.

#include "game/progression.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr int current_save_version = 6;

struct repeatable_collectible_state {
    int claim_count = 0;
    // save_data::holes_completed when last claimed; nullopt when never claimed
    // (or claimed before save version 6, which had no hole counter).
    std::optional<int> claimed_at_holes_completed;
};

struct save_data {
    int version = current_save_version;
    std::vector<std::string> completed_course_ids;
    // Holes finished over the save's lifetime; the clock for collectible cooldowns.
    int holes_completed = 0;
    skill_progression skills;
    std::vector<std::string> collected_ids;
    std::map<std::string, repeatable_collectible_state> repeatable_collectibles;
    std::vector<std::string> world_flags;
};

save_data migrate_save_data(save_data save);
// Parses any version and migrates it. nullopt for invalid JSON or a version
// newer than current_save_version (never downgrade a newer save).
std::optional<save_data> parse_save_data(const std::string& text);
std::string save_data_to_json(const save_data& save);
