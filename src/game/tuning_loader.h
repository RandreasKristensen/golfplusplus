#pragma once

// Reads the files of assets/play/tuning/ into game_tuning. Each file holds
// whole sections of it ("ball", "lies"), each section in one file only.

#include "game/game_tuning.h"

#include <optional>
#include <string>
#include <vector>

// The folder of tuning files under the asset root.
inline constexpr const char* tuning_path = "play/tuning";

struct game_tuning_parse_result {
    std::optional<game_tuning> tuning;
    // Names every missing or malformed field ("ball.stop_speed") on failure.
    std::string error;
};

// Every field of game_tuning must be present.
game_tuning_parse_result parse_game_tuning_from_text(const std::string& text);
// The same over the texts of tuning_path's files, their sections merged; a
// section in two of them fails.
game_tuning_parse_result parse_game_tuning_from_texts(const std::vector<std::string>& texts);
