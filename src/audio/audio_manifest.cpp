#include "audio/audio_manifest.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>

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

std::optional<float> float_at(const json& object, const char* key) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_number()) {
        return std::nullopt;
    }
    return it->get<float>();
}

audio_sound_type sound_type_from_string(const std::string& value) {
    if (value == "loop") {
        return audio_sound_type::loop;
    }
    if (value == "ambience") {
        return audio_sound_type::ambience;
    }
    return audio_sound_type::sfx;
}

float clamped_volume(const float volume) {
    return std::max(0.0f, std::min(1.0f, volume));
}

float non_negative_multiplier(const float multiplier) {
    return std::max(0.0f, multiplier);
}

audio_manifest_parse_result fail(const std::string& error) {
    audio_manifest_parse_result result;
    result.error = error;
    return result;
}
}

audio_manifest_parse_result parse_audio_manifest(const std::string& text) {
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return fail("audio manifest is not valid JSON object");
    }

    const auto sounds_it = root.find("sounds");
    if (sounds_it == root.end() || !sounds_it->is_array()) {
        return fail("audio manifest is missing sounds array");
    }

    audio_manifest manifest;
    manifest.master_volume = clamped_volume(float_at(root, "master_volume").value_or(1.0f));

    const auto categories_it = root.find("categories");
    if (categories_it != root.end()) {
        if (!categories_it->is_object()) {
            return fail("audio manifest categories must be an object");
        }
        for (const auto& item : categories_it->items()) {
            if (!item.value().is_number()) {
                return fail("audio manifest category volume must be numeric");
            }
            manifest.category_volumes[item.key()] = clamped_volume(item.value().get<float>());
        }
    }

    std::unordered_set<std::string> ids;
    for (const json& sound_json : *sounds_it) {
        if (!sound_json.is_object()) {
            return fail("audio manifest sound entry must be an object");
        }

        audio_sound_definition sound;
        sound.id = string_at(sound_json, "id").value_or("");
        sound.file = string_at(sound_json, "file").value_or("");
        if (sound.id.empty() || sound.file.empty()) {
            return fail("audio manifest sound entries require id and file");
        }
        if (!ids.insert(sound.id).second) {
            return fail("audio manifest has duplicate sound id: " + sound.id);
        }

        sound.category = string_at(sound_json, "category").value_or("gameplay");
        sound.description = string_at(sound_json, "description").value_or("");
        sound.target_length_seconds = std::max(0.0f, float_at(sound_json, "target_length_seconds").value_or(0.0f));
        sound.volume_multiplier = non_negative_multiplier(float_at(sound_json, "volume_multiplier").value_or(1.0f));
        sound.type = sound_type_from_string(string_at(sound_json, "type").value_or("sfx"));
        manifest.sounds.push_back(sound);
    }

    audio_manifest_parse_result result;
    result.manifest = manifest;
    return result;
}

audio_manifest_parse_result load_audio_manifest_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        return fail("audio manifest could not be opened: " + path);
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return parse_audio_manifest(buffer.str());
}
