#include "audio/audio_manifest.h"

#include "game/content_files.h"
#include "game/json_util.h"

#include <algorithm>
#include <unordered_set>

namespace {
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
    return std::clamp(volume, 0.0f, 1.0f);
}

audio_manifest_parse_result fail(const std::string& error) {
    audio_manifest_parse_result result;
    result.error = error;
    return result;
}
}

audio_levels audio_levels_from_settings(const settings_values& values, const std::vector<setting_definition>& definitions) {
    audio_levels levels;
    if (const setting_definition* master = find_setting(definitions, setting_master_volume)) {
        levels.master = setting_fraction(values, *master);
    }
    if (const setting_definition* music = find_setting(definitions, setting_music_volume)) {
        levels.music = setting_fraction(values, *music);
    }
    return levels;
}

float sound_mix_gain(const audio_manifest& manifest, const audio_sound_definition& sound) {
    const auto it = manifest.category_volumes.find(sound.category);
    const float category = it != manifest.category_volumes.end() ? it->second : 1.0f;
    return clamped_volume(manifest.master_volume * category * sound.volume_multiplier);
}

float played_gain(const float mix_gain, const audio_sound_type type, const audio_levels& levels) {
    const float music = type == audio_sound_type::ambience ? clamped_volume(levels.music) : 1.0f;
    return clamped_volume(mix_gain) * clamped_volume(levels.master) * music;
}

audio_manifest_parse_result parse_audio_manifest(const std::string& text) {
    const std::optional<json> root = parse_json(text);
    if (!root || !root->is_object()) {
        return fail("audio manifest is not a JSON object");
    }
    const json* sounds = json_array(*root, "sounds");
    if (sounds == nullptr) {
        return fail("audio manifest is missing the sounds array");
    }

    audio_manifest manifest;
    manifest.master_volume = clamped_volume(json_float(*root, "master_volume").value_or(1.0f));

    if (root->contains("categories")) {
        const json* categories = json_object(*root, "categories");
        if (categories == nullptr) {
            return fail("audio manifest categories must be an object");
        }
        for (auto it = categories->begin(); it != categories->end(); ++it) {
            if (!it.value().is_number()) {
                return fail("audio manifest category volume must be numeric");
            }
            manifest.category_volumes[it.key()] = clamped_volume(it.value().get<float>());
        }
    }

    std::unordered_set<std::string> ids;
    for (const json& entry : *sounds) {
        if (!entry.is_object()) {
            return fail("audio manifest sound entry must be an object");
        }

        audio_sound_definition sound;
        sound.id = json_string(entry, "id").value_or("");
        sound.file = json_string(entry, "file").value_or("");
        if (sound.id.empty() || sound.file.empty()) {
            return fail("audio manifest sound entries require id and file");
        }
        if (!ids.insert(sound.id).second) {
            return fail("audio manifest has duplicate sound id: " + sound.id);
        }
        sound.category = json_string(entry, "category").value_or("gameplay");
        sound.volume_multiplier = std::max(0.0f, json_float(entry, "volume_multiplier").value_or(1.0f));
        sound.type = sound_type_from_string(json_string(entry, "type").value_or("sfx"));
        manifest.sounds.push_back(sound);
    }

    audio_manifest_parse_result result;
    result.manifest = manifest;
    return result;
}

audio_manifest_parse_result load_audio_manifest_from_file(const std::string& path) {
    const std::optional<std::string> text = read_text_file(path);
    return text ? parse_audio_manifest(*text) : fail("audio manifest could not be opened: " + path);
}
