#pragma once

// assets/audio/sounds.json. Entries may carry authoring notes
// ("description", "target_length_seconds") that the game ignores.

#include "game/settings.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

enum class audio_sound_type {
    sfx,
    loop,
    ambience  // streamed music, one at a time
};

struct audio_sound_definition {
    std::string id;
    std::string file;  // relative to the manifest's folder
    std::string category;
    float volume_multiplier = 1.0f;
    audio_sound_type type = audio_sound_type::sfx;
};

struct audio_manifest {
    float master_volume = 1.0f;
    std::unordered_map<std::string, float> category_volumes;
    std::vector<audio_sound_definition> sounds;
};

struct audio_manifest_parse_result {
    std::optional<audio_manifest> manifest;
    std::string error;
};

// The player's volume settings, each 0..1: master scales every sound,
// music scales the ambience on top of master.
struct audio_levels {
    float master = 1.0f;
    float music = 1.0f;
};

// From the master and music volume settings, each at its fraction of its range.
audio_levels audio_levels_from_settings(const settings_values& values, const std::vector<setting_definition>& definitions);

// A sound's level in the mix (0..1) from the manifest alone: its master,
// category and multiplier.
float sound_mix_gain(const audio_manifest& manifest, const audio_sound_definition& sound);
// What a sound with `mix_gain` plays at under the player's levels (0..1).
float played_gain(float mix_gain, audio_sound_type type, const audio_levels& levels);

audio_manifest_parse_result parse_audio_manifest(const std::string& text);
audio_manifest_parse_result load_audio_manifest_from_file(const std::string& path);
