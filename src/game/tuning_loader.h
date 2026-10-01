#pragma once

// Reads assets/tuning/game_tuning.json into game_tuning.

#include "game/game_tuning.h"

#include <optional>
#include <string>

inline constexpr const char* game_tuning_path = "tuning/game_tuning.json";

struct game_tuning_parse_result {
    std::optional<game_tuning> tuning;
    // Names every missing or malformed field ("ball.stop_speed") on failure.
    std::string error;
};

// Every field of game_tuning must be present.
game_tuning_parse_result parse_game_tuning_from_text(const std::string& text);
