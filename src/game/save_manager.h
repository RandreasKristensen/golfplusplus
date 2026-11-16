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
    // Set when a save file existed but could not be read or parsed. Each such
    // file is copied aside (`unreadable_backup` is the first copy) before
    // anything can replace it.
    bool existing_was_unreadable = false;
    std::filesystem::path unreadable_backup;
    // Set when the primary file was missing or unreadable and the save came
    // from the file an interrupted write left behind (content_files.h).
    bool recovered_from_interrupted_write = false;
};

// A fresh save_data only when no save file exists, or none is readable (those
// are kept as `unreadable_backup`). A write interrupted by a crash is
// recovered: a valid primary wins, then the complete temp file, then the backup.
save_load_result load_save(const std::filesystem::path& path);

// Writes through a temporary file so a crash never leaves a half-written save.
bool write_save(const std::filesystem::path& path, const save_data& save);
