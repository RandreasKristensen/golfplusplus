#pragma once

// assets/audio/sounds.json. Entries may carry authoring notes
// ("description", "target_length_seconds") that the game ignores.

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

audio_manifest_parse_result parse_audio_manifest(const std::string& text);
audio_manifest_parse_result load_audio_manifest_from_file(const std::string& path);
