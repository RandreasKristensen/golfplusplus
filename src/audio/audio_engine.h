#pragma once

// Plays the sounds in assets/audio/sounds.json through SDL_mixer. Missing
// files and failed loads are logged once and then ignored, so the game runs
// without audio.

#include "audio/audio_manifest.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

struct Mix_Chunk;
struct Mix_Music;

class audio_engine {
public:
    audio_engine() = default;
    audio_engine(const audio_engine&) = delete;
    audio_engine& operator=(const audio_engine&) = delete;
    ~audio_engine();

    bool init();
    bool load_manifest(const std::filesystem::path& manifest_path);
    void play(const std::string& id);
    // Starts a looping sound unless it is already playing.
    void play_loop(const std::string& id);
    void stop_loop(const std::string& id);
    // Switches the background ambience (no-op if it is already playing).
    void start_ambience(const std::string& id);
    // The player's volume levels, applied live: to every loaded sound,
    // including those playing and looping now, and to the ambience.
    void set_levels(const audio_levels& levels);
    void shutdown();

private:
    struct chunk_deleter {
        void operator()(Mix_Chunk* chunk) const;
    };
    struct music_deleter {
        void operator()(Mix_Music* music) const;
    };
    struct loaded_chunk {
        std::unique_ptr<Mix_Chunk, chunk_deleter> chunk;
        float mix_gain = 0.0f;  // sound_mix_gain
        audio_sound_type type = audio_sound_type::sfx;
    };
    struct loaded_music {
        std::unique_ptr<Mix_Music, music_deleter> music;
        float mix_gain = 0.0f;  // sound_mix_gain
    };

    void unload_manifest();
    void warn_once(const std::string& key, const std::string& message);

    bool initialized_ = false;
    bool mixer_open_ = false;
    std::unordered_map<std::string, loaded_chunk> chunks_;
    std::unordered_map<std::string, loaded_music> music_;
    std::unordered_map<std::string, int> loop_channels_;
    std::unordered_set<std::string> warned_;
    std::string active_ambience_;
    audio_levels levels_;
};
