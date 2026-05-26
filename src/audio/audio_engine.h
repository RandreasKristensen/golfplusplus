#pragma once

#include "audio/audio_manifest.h"

#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct Mix_Chunk;
struct Mix_Music;

class audio_engine {
public:
    bool init();
    bool load_manifest(const std::filesystem::path& manifest_path);
    void play(const std::string& id);
    void play_loop(const std::string& id);
    void stop_loop(const std::string& id);
    void start_ambience(const std::string& id);
    void stop_ambience();
    void set_category_volume(const std::string& category, float volume);
    void shutdown();

private:
    struct loaded_chunk {
        Mix_Chunk* chunk = nullptr;
        std::string category;
        float volume_multiplier = 1.0f;
    };

    struct loaded_music {
        Mix_Music* music = nullptr;
        std::string category;
        float volume_multiplier = 1.0f;
    };

    void unload_manifest();
    void warn_once(const std::string& key, const std::string& message);
    int volume_for_sound(const std::string& category, float volume_multiplier) const;
    void refresh_chunk_volumes();

    bool initialized_ = false;
    bool mixer_open_ = false;
    float master_volume_ = 1.0f;
    std::unordered_map<std::string, float> category_volumes_;
    std::unordered_map<std::string, loaded_chunk> chunks_;
    std::unordered_map<std::string, loaded_music> music_;
    std::unordered_map<std::string, int> loop_channels_;
    std::unordered_set<std::string> warned_;
    std::string active_ambience_;
};
