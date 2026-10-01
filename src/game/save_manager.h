#pragma once

// Reads and writes the offline save file. Save on hole completion, course
// completion, leaving a round and clean exit; never mid-hole.

#include "game/save_data.h"

#include <filesystem>
#include <string>

// <save_root>/profiles/local/slot_0/save.json
std::filesystem::path save_file_path(const std::filesystem::path& save_root);

struct save_load_result {
    save_data save;
    bool loaded_existing = false;
    // Set when a save file existed but could not be read or parsed. It is
    // copied to `unreadable_backup` before a fresh save replaces it.
    bool existing_was_unreadable = false;
    std::filesystem::path unreadable_backup;
};

// A fresh save_data when the file is missing or unreadable.
save_load_result load_save(const std::filesystem::path& path);

// Writes through a temporary file so a crash never leaves a half-written save.
bool write_save(const std::filesystem::path& path, const save_data& save);
