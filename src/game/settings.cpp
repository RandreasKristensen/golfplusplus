#include "game/settings.h"

#include "game/content_files.h"
#include "game/json_util.h"

#include <algorithm>
#include <utility>

namespace {
constexpr int settings_file_version = 1;

std::optional<setting_definition> setting_from_json(const json& value) {
    const std::optional<std::string> id = json_string(value, "id");
    const std::optional<std::string> label = json_string(value, "label");
    const std::optional<std::string> value_text = json_string(value, "value");
    const std::optional<int> min = json_int(value, "min");
    const std::optional<int> max = json_int(value, "max");
    const std::optional<int> step = json_int(value, "step");
    const std::optional<int> default_value = json_int(value, "default");
    if (!id || id->empty() || !label || label->empty() || !value_text || value_text->empty() || !min || !max ||
        !step || !default_value || *min >= *max || *step <= 0 || *default_value < *min || *default_value > *max) {
        return std::nullopt;
    }
    return setting_definition{*id, *label, *value_text, *min, *max, *step, *default_value};
}
}

std::optional<std::vector<setting_definition>> parse_setting_definitions_from_text(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    const json* settings = root ? json_array(*root, "settings") : nullptr;
    if (settings == nullptr) {
        return std::nullopt;
    }
    std::vector<setting_definition> definitions;
    for (const json& value : *settings) {
        std::optional<setting_definition> definition = setting_from_json(value);
        if (!definition || find_setting(definitions, definition->id) != nullptr) {
            return std::nullopt;
        }
        definitions.push_back(std::move(*definition));
    }
    return definitions;
}

const setting_definition* find_setting(const std::vector<setting_definition>& definitions, const std::string& id) {
    const auto it = std::find_if(definitions.begin(), definitions.end(),
                                 [&id](const setting_definition& definition) { return definition.id == id; });
    return it != definitions.end() ? &*it : nullptr;
}

bool has_applied_settings(const std::vector<setting_definition>& definitions) {
    return find_setting(definitions, setting_master_volume) != nullptr &&
        find_setting(definitions, setting_music_volume) != nullptr &&
        find_setting(definitions, setting_field_of_view) != nullptr;
}

int setting_value(const settings_values& values, const setting_definition& definition) {
    const auto it = values.find(definition.id);
    return std::clamp(it != values.end() ? it->second : definition.default_value, definition.min, definition.max);
}

float setting_fraction(const settings_values& values, const setting_definition& definition) {
    return static_cast<float>(setting_value(values, definition) - definition.min) /
           static_cast<float>(definition.max - definition.min);
}

settings_values adjust_setting(const settings_values& values, const setting_definition& definition, const int steps) {
    settings_values result = values;
    result[definition.id] =
        std::clamp(setting_value(values, definition) + steps * definition.step, definition.min, definition.max);
    return result;
}

std::optional<settings_values> parse_settings_from_text(const std::string& text,
                                                        const std::vector<setting_definition>& definitions) {
    const std::optional<json> root = parse_json(text);
    const json* stored = root ? json_object(*root, "values") : nullptr;
    if (stored == nullptr) {
        return std::nullopt;
    }
    settings_values values;
    for (const setting_definition& definition : definitions) {
        if (const std::optional<int> value = json_int(*stored, definition.id.c_str())) {
            values[definition.id] = std::clamp(*value, definition.min, definition.max);
        }
    }
    return values;
}

std::string settings_to_text(const settings_values& values) {
    json root = json::object();
    root["version"] = settings_file_version;
    json stored = json::object();
    for (const auto& [id, value] : values) {
        stored[id] = value;
    }
    root["values"] = std::move(stored);
    return root.dump(2) + "\n";
}

std::filesystem::path settings_file_path(const std::filesystem::path& save_root) {
    return save_root / "settings.json";
}

settings_values load_settings(const std::filesystem::path& path, const std::vector<setting_definition>& definitions) {
    const std::optional<std::string> text = read_text_file(path);
    return text ? parse_settings_from_text(*text, definitions).value_or(settings_values{}) : settings_values{};
}

bool write_settings(const std::filesystem::path& path, const settings_values& values) {
    return replace_text_file(path, settings_to_text(values));
}
