#pragma once

// Where and whether the app writes the offline save, and the moments it does
// (documented in save_manager.h). Online progress lives on the server and is
// never written here.

#include "game/game_state.h"

#include <filesystem>
#include <string>

struct progress_saver {
    std::filesystem::path path;
    // False for review runs, and when an unreadable save could not be kept.
    bool enabled = true;
};

// Writes the offline save and clears save_requested. True unless a write failed.
bool save_progress(game_state& state, const progress_saver& saver);

// enter_place, and when that confirms the bus from a course to the hub, a
// save_progress: XP earned since the last hole survives a crash in the hub.
bool enter_place_saving(game_state& state, const std::string& place_id, const progress_saver& saver);
