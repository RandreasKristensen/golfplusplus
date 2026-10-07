#include "game/save_manager.h"

#include "game/content_files.h"
#include "game/json_util.h"

#include <chrono>
#include <system_error>

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
    return replace_text_file(path, save_data_to_json(save));
}
