#pragma once

#include "game/save_data.h"

#include <cstdint>
#include <optional>
#include <string>

constexpr int profile_metadata_version = 1;

struct local_profile_metadata {
    int version = profile_metadata_version;
    std::string profile_id = "local";
    std::string profile_name = "Local Player";
    std::string slot_id = "slot_0";
    std::string remote_account_id;
    std::uint64_t client_revision = 0;
    std::string last_saved_utc;
    bool dirty = false;
};

struct save_paths {
    std::string root_directory;
    std::string profile_metadata_path;
    std::string save_data_path;
};

struct save_slot {
    local_profile_metadata profile;
    save_data save;
    bool loaded_existing_profile = false;
    bool loaded_existing_save = false;
};

save_paths default_save_paths(const std::string& save_root);
std::optional<local_profile_metadata> parse_profile_metadata(const std::string& text);
std::string profile_metadata_to_json(const local_profile_metadata& profile);
std::optional<local_profile_metadata> load_profile_metadata_from_file(const std::string& path);
save_slot load_or_create_save_slot(const save_paths& paths, const save_data& fallback_save);
void mark_save_slot_dirty(save_slot& slot, const save_data& save);
bool persist_save_slot(const save_paths& paths, save_slot& slot);
