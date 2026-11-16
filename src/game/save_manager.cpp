#include "game/save_manager.h"

#include "game/content_files.h"
#include "game/json_util.h"

#include <chrono>
#include <system_error>

std::filesystem::path save_file_path(const std::filesystem::path& save_root) {
    return save_root / "profiles" / "local" / "slot_0" / "save.json";
}

namespace {
// Copies every existing file that is not a valid save to `<file>.unreadable-<stamp>`
// (a valid one that was not used stays where it is). An empty unreadable_backup
// with existing_was_unreadable set means a copy failed, which stops saving.
void keep_unreadable_files(const std::filesystem::path& path, const replaced_source used, save_load_result& result) {
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::pair<std::filesystem::path, replaced_source> files[] = {
        {path, replaced_source::primary},
        {replacement_temp_path(path), replaced_source::temp},
        {replacement_backup_path(path), replaced_source::backup}};
    bool copy_failed = false;
    for (const auto& [file, source] : files) {
        const std::optional<std::string> text = read_text_file(file);
        if (source == used || !text || parse_save_data(*text)) {
            continue;
        }
        std::error_code error;
        const std::filesystem::path copy = file.string() + ".unreadable-" + std::to_string(stamp);
        std::filesystem::copy_file(file, copy, std::filesystem::copy_options::overwrite_existing, error);
        if (error) {
            copy_failed = true;
        } else if (result.unreadable_backup.empty()) {
            result.unreadable_backup = copy;
        }
        result.existing_was_unreadable = true;
    }
    if (copy_failed) {
        result.unreadable_backup.clear();
    }
}
}

save_load_result load_save(const std::filesystem::path& path) {
    save_load_result result;
    std::optional<save_data> save;
    const replaced_text found = read_replaced_text_file(path, [&save](const std::string& text) {
        save = parse_save_data(text);
        return save.has_value();
    });
    if (found.text) {
        result.save = *save;
        result.loaded_existing = true;
        result.recovered_from_interrupted_write = found.source != replaced_source::primary;
    }
    // Leftovers beside a valid primary are only the residue of an interrupted write.
    if (found.source != replaced_source::primary) {
        keep_unreadable_files(path, found.source, result);
    }
    return result;
}

bool write_save(const std::filesystem::path& path, const save_data& save) {
    return replace_text_file(path, save_data_to_json(save));
}
