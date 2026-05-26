#include "game/save_manager.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include <nlohmann/json.hpp>

namespace {
using json = nlohmann::json;

std::optional<std::string> string_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

std::optional<int> int_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_integer()) {
        return std::nullopt;
    }
    return it->get<int>();
}

std::optional<std::uint64_t> uint64_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number_unsigned()) {
        return std::nullopt;
    }
    return it->get<std::uint64_t>();
}

bool bool_at(const json& object, const char* key, const bool fallback) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_boolean()) {
        return fallback;
    }
    return it->get<bool>();
}

std::string read_text_file(const std::string& path) {
    std::ifstream file{std::filesystem::path(path)};
    if (!file) {
        return "";
    }

    std::ostringstream stream;
    stream << file.rdbuf();
    return stream.str();
}

std::string current_utc_timestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif

    std::ostringstream stream;
    stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return stream.str();
}

bool write_text_atomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path temp_path = path.string() + ".tmp";
    const std::filesystem::path backup_path = path.string() + ".bak";
    {
        std::ofstream file{temp_path, std::ios::trunc};
        if (!file) {
            return false;
        }
        file << text;
        if (!file) {
            return false;
        }
    }

    error.clear();
    if (std::filesystem::exists(path, error)) {
        std::filesystem::copy_file(path, backup_path, std::filesystem::copy_options::overwrite_existing, error);
        error.clear();
        std::filesystem::remove(path, error);
        if (error) {
            std::filesystem::remove(temp_path);
            return false;
        }
    }

    error.clear();
    std::filesystem::rename(temp_path, path, error);
    if (error) {
        std::filesystem::copy_file(backup_path, path, std::filesystem::copy_options::overwrite_existing, error);
        std::filesystem::remove(temp_path);
        return false;
    }

    std::filesystem::remove(backup_path, error);
    return true;
}

local_profile_metadata migrate_profile_metadata(local_profile_metadata profile) {
    if (profile.version < 1) {
        profile.version = 1;
    }
    profile.version = profile_metadata_version;
    if (profile.profile_id.empty()) {
        profile.profile_id = "local";
    }
    if (profile.profile_name.empty()) {
        profile.profile_name = "Local Player";
    }
    if (profile.slot_id.empty()) {
        profile.slot_id = "slot_0";
    }
    return profile;
}
}

save_paths default_save_paths(const std::string& save_root) {
    const std::filesystem::path root = save_root.empty()
        ? std::filesystem::path("saves")
        : std::filesystem::path(save_root);
    const std::filesystem::path slot_root = root / "profiles" / "local" / "slot_0";

    save_paths paths;
    paths.root_directory = root.string();
    paths.profile_metadata_path = (slot_root / "profile.json").string();
    paths.save_data_path = (slot_root / "save.json").string();
    return paths;
}

std::optional<local_profile_metadata> parse_profile_metadata(const std::string& text) {
    json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::nullopt;
    }

    local_profile_metadata profile;
    profile.version = int_at(root, "version").value_or(0);
    profile.profile_id = string_at(root, "profile_id").value_or(profile.profile_id);
    profile.profile_name = string_at(root, "profile_name").value_or(profile.profile_name);
    profile.slot_id = string_at(root, "slot_id").value_or(profile.slot_id);
    profile.remote_account_id = string_at(root, "remote_account_id").value_or("");
    profile.client_revision = uint64_at(root, "client_revision").value_or(0);
    profile.last_saved_utc = string_at(root, "last_saved_utc").value_or("");
    profile.dirty = bool_at(root, "dirty", false);
    return migrate_profile_metadata(profile);
}

std::string profile_metadata_to_json(const local_profile_metadata& profile) {
    json root = json::object();
    root["version"] = profile_metadata_version;
    root["profile_id"] = profile.profile_id.empty() ? "local" : profile.profile_id;
    root["profile_name"] = profile.profile_name.empty() ? "Local Player" : profile.profile_name;
    root["slot_id"] = profile.slot_id.empty() ? "slot_0" : profile.slot_id;
    root["remote_account_id"] = profile.remote_account_id;
    root["client_revision"] = profile.client_revision;
    root["last_saved_utc"] = profile.last_saved_utc;
    root["dirty"] = profile.dirty;
    return root.dump(2) + "\n";
}

std::optional<local_profile_metadata> load_profile_metadata_from_file(const std::string& path) {
    const std::string text = read_text_file(path);
    if (text.empty()) {
        return std::nullopt;
    }
    return parse_profile_metadata(text);
}

save_slot load_or_create_save_slot(const save_paths& paths, const save_data& fallback_save) {
    save_slot slot;
    const std::optional<local_profile_metadata> profile = load_profile_metadata_from_file(paths.profile_metadata_path);
    if (profile) {
        slot.profile = *profile;
        slot.loaded_existing_profile = true;
    }

    const std::optional<save_data> save = load_save_data_from_file(paths.save_data_path);
    if (save) {
        slot.save = *save;
        slot.loaded_existing_save = true;
    } else {
        slot.save = migrate_save_data(fallback_save);
    }

    return slot;
}

void mark_save_slot_dirty(save_slot& slot, const save_data& save) {
    slot.save = migrate_save_data(save);
    slot.profile.client_revision = std::max<std::uint64_t>(1, slot.profile.client_revision + 1);
    slot.profile.last_saved_utc = current_utc_timestamp();
    slot.profile.dirty = true;
}

bool persist_save_slot(const save_paths& paths, save_slot& slot) {
    slot.save = migrate_save_data(slot.save);
    if (slot.profile.last_saved_utc.empty()) {
        slot.profile.last_saved_utc = current_utc_timestamp();
    }

    const bool save_written = write_text_atomically(paths.save_data_path, save_data_to_json(slot.save));
    if (!save_written) {
        return false;
    }

    slot.profile.dirty = false;
    const bool profile_written = write_text_atomically(paths.profile_metadata_path, profile_metadata_to_json(slot.profile));
    if (!profile_written) {
        slot.profile.dirty = true;
        return false;
    }

    slot.loaded_existing_save = true;
    slot.loaded_existing_profile = true;
    return true;
}
