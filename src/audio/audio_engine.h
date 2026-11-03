#pragma once

// Plays the sounds in assets/sound/sounds.json through SDL_mixer, those in
// the world from where they happen (audio/sound_placement.h). Missing files
// and failed loads are logged once and then ignored, so the game runs
// without audio.

#include "audio/audio_manifest.h"
#include "audio/sound_placement.h"

#include <atomic>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <glm/vec3.hpp>

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
    // Where placed sounds are heard from from now on: the frame's camera.
    void set_listener(const sound_listener& listener, float meters_per_world_unit);
    // Plays a sound at full level, centred.
    void play(const std::string& id);
    // Plays a sound from `position` in the world, if its manifest entry is
    // placed (else as play).
    void play_at(const std::string& id, const glm::vec3& position);
    // Starts the looping sound `id` from `position` as the loop `key`, unless
    // that loop is already playing. Several loops may play one sound.
    void play_loop(const std::string& key, const std::string& id, const glm::vec3& position);
    // Moves a playing loop to `position`.
    void place_loop(const std::string& key, const glm::vec3& position);
    void stop_loop(const std::string& key);
    // Plays a loop faster (higher) or slower (lower) by `ratio` from now,
    // while it plays.
    void set_loop_pitch(const std::string& key, float ratio);
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
        std::optional<sound_spread> spread;
    };
    struct loaded_music {
        std::unique_ptr<Mix_Music, music_deleter> music;
        float mix_gain = 0.0f;  // sound_mix_gain
    };

    // A loop played back at its own rate (play_loop, set_loop_pitch): the
    // mixer's effect reads its chunk from `position` (frames) on, stepping
    // `ratio` frames each frame.
    struct pitched_loop {
        const Mix_Chunk* chunk = nullptr;
        double position = 0.0;
        std::atomic<float> ratio{1.0f};
    };

    struct playing_loop {
        int channel = -1;
        std::string id;
    };

    static void play_pitched(int channel, void* stream, int length, void* loop_data);
    // Sets `channel`'s level and pan for `sound` heard from `position`, or
    // full and centred with none.
    void mix_channel(int channel, const loaded_chunk& sound, const std::optional<glm::vec3>& position) const;
    // Starts `sound` on a free channel mixed as mix_channel says: its channel.
    std::optional<int> start(const loaded_chunk& sound, const std::optional<glm::vec3>& position, int loops) const;
    void unload_manifest();
    void warn_once(const std::string& key, const std::string& message);

    bool initialized_ = false;
    bool mixer_open_ = false;
    std::unordered_map<std::string, loaded_chunk> chunks_;
    std::unordered_map<std::string, loaded_music> music_;
    std::unordered_map<std::string, playing_loop> loops_;
    std::unordered_map<std::string, std::unique_ptr<pitched_loop>> pitched_loops_;
    std::unordered_set<std::string> warned_;
    std::string active_ambience_;
    audio_levels levels_;
    sound_listener listener_;
    float meters_per_world_unit_ = 1.0f;
};
