#pragma once

// assets/sound/sounds.json. Entries may carry authoring notes
// ("description", "target_length_seconds") that the game ignores. Each
// sound either carries in the world (reference_distance and max_distance,
// metres, picked by ear) or says "placed": false and plays at full level,
// centred (menus, the HUD, thunder, the ambience). "voices" shares the
// mixer's channels (audio/voice_pool.h); "underwater" shapes the mix
// while the camera is under water (audio/underwater_filter.h).

#include "audio/sound_placement.h"
#include "audio/underwater_filter.h"
#include "game/settings.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// The manifest under the asset root; its sounds' files are beside it.
inline constexpr const char* sounds_path = "sound/sounds.json";

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
    std::optional<sound_spread> spread;  // none: not placed
};

// The mixer's channels: `channels` in all, of which loops hold at most
// `loops`, leaving the rest for one-shot sounds.
struct audio_voices {
    int channels = 0;
    int loops = 0;
};

struct audio_manifest {
    float master_volume = 1.0f;
    audio_voices voices;
    underwater_sound underwater;
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
