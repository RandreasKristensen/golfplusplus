#pragma once

// Player settings (master and music volume, field of view, ...): which settings exist
// and their ranges come from assets/ui/settings.json; the player's values
// live in their own file next to the save, not in it, because the save is offline
// progress and settings apply online too. A setting is a whole number
// stepped between a minimum and a maximum; the settings screen
// (core/settings_menu.h) lists every definition in file order.

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

inline constexpr const char* setting_definitions_path = "ui/settings.json";

// The settings code applies; load_game_content refuses a definitions file
// without them.
inline constexpr const char* setting_master_volume = "master_volume";  // every sound, music included
inline constexpr const char* setting_music_volume = "music_volume";
inline constexpr const char* setting_field_of_view = "field_of_view";  // the gameplay camera's, in degrees

struct setting_definition {
    std::string id;
    std::string label_key;  // string table keys: the tile's title...
    std::string value_key;  // ...and its value, formatted with {value}
    int min = 0;
    int max = 0;
    int step = 0;
    int default_value = 0;
};

// The player's values by setting id. A setting missing here has its default.
using settings_values = std::map<std::string, int>;

// nullopt when the text is not a valid definitions file: every setting
// needs an id, its text keys, min < max, a positive step and a default in
// range, and ids are unique.
std::optional<std::vector<setting_definition>> parse_setting_definitions_from_text(const std::string& text);

// Whether `definitions` has every setting the code applies.
bool has_applied_settings(const std::vector<setting_definition>& definitions);

const setting_definition* find_setting(const std::vector<setting_definition>& definitions, const std::string& id);

// The value of `definition`, its default when unset, always in range.
int setting_value(const settings_values& values, const setting_definition& definition);
// Where the value sits between min (0) and max (1).
float setting_fraction(const settings_values& values, const setting_definition& definition);
// Moves the value by `steps` steps, clamped to the range.
settings_values adjust_setting(const settings_values& values, const setting_definition& definition, int steps);

// Values for the known settings in the text, clamped to their ranges;
// unknown ids are dropped. nullopt when the text is not a settings file.
std::optional<settings_values> parse_settings_from_text(const std::string& text,
                                                        const std::vector<setting_definition>& definitions);
std::string settings_to_text(const settings_values& values);

// <save_root>/settings.json
std::filesystem::path settings_file_path(const std::filesystem::path& save_root);
// Empty (every default) when the file is missing or unreadable: settings
// are not progress, so a bad file is simply replaced on the next change.
settings_values load_settings(const std::filesystem::path& path, const std::vector<setting_definition>& definitions);
bool write_settings(const std::filesystem::path& path, const settings_values& values);
