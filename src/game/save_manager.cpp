#include "game/save_manager.h"

#include "game/json_util.h"

#include <chrono>
#include <fstream>
#include <system_error>

namespace {
bool write_text_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << text;
    return static_cast<bool>(file);
}

// Replaces `path` with `temp`, keeping the old file as `.bak` until the
// replacement succeeded (rename cannot overwrite an existing file everywhere).
bool replace_file(const std::filesystem::path& temp, const std::filesystem::path& path) {
    std::error_code error;
    const std::filesystem::path backup = path.string() + ".bak";
    const bool had_old = std::filesystem::exists(path, error);
    if (had_old) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            std::filesystem::remove(temp, error);
            return false;
        }
    }

    std::filesystem::rename(temp, path, error);
    if (error) {
        std::error_code restore_error;
        if (had_old) {
            std::filesystem::rename(backup, path, restore_error);
        }
        std::filesystem::remove(temp, restore_error);
        return false;
    }

    if (had_old) {
        std::filesystem::remove(backup, error);
    }
    return true;
}
}

std::filesystem::path save_file_path(const std::filesystem::path& save_root) {
    return save_root / "profiles" / "local" / "slot_0" / "save.json";
}

save_load_result load_save(const std::filesystem::path& path) {
    save_load_result result;
    std::error_code error;
    if (!std::filesystem::exists(path, error)) {
        return result;
    }

    const std::optional<std::string> text = read_text_file(path);
    const std::optional<save_data> save = text ? parse_save_data(*text) : std::nullopt;
    if (save) {
        result.save = *save;
        result.loaded_existing = true;
        return result;
    }

    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    result.existing_was_unreadable = true;
    result.unreadable_backup = path.string() + ".unreadable-" + std::to_string(stamp);
    std::filesystem::copy_file(path, result.unreadable_backup, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        result.unreadable_backup.clear();
    }
    return result;
}

bool write_save(const std::filesystem::path& path, const save_data& save) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        return false;
    }

    const std::filesystem::path temp = path.string() + ".tmp";
    if (!write_text_file(temp, save_data_to_json(save))) {
        std::filesystem::remove(temp, error);
        return false;
    }
    return replace_file(temp, path);
}
