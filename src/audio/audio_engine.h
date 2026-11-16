#pragma once

// Plays the sounds in assets/sound/sounds.json through SDL_mixer, those in
// the world from where they happen (audio/sound_placement.h), on channels
// shared out by audio/voice_pool.h. Missing files and failed loads are logged
// once and then ignored, so the game runs without audio.

#include "audio/audio_manifest.h"
#include "audio/sound_placement.h"
#include "audio/underwater_filter.h"
#include "audio/voice_pool.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/vec3.hpp>

struct Mix_Chunk;
struct Mix_Music;

// A looping sound heard from `position` in the world, at `pitch` times its rate.
struct loop_request {
    std::string key;
    std::string sound;
    glm::vec3 position{0.0f};
    float pitch = 1.0f;
};

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
    // The loops heard from now, by key (several may play one sound): each
    // starts or keeps playing from its position at its pitch; any other
    // stops. Out of earshot or past the loops' share a loop is silent.
    void set_loops(const std::vector<loop_request>& loops);
    // Switches the background ambience (no-op if it is already playing).
    void start_ambience(const std::string& id);
    // The player's volume levels, applied live: to every loaded sound,
    // including those playing and looping now, and to the ambience.
    void set_levels(const audio_levels& levels);
    // Whether the listener is under water: everything heard is muffled,
    // glided in and out (audio/underwater_filter.h).
    void set_underwater(bool underwater);
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

    // SDL_mixer's channels as voices_ drives them.
    class sdl_channels final : public mixer_channels {
    public:
        explicit sdl_channels(const audio_engine& engine) : engine_(engine) {}
        bool playing(int channel) const override;
        bool start(int channel, const std::string& sound, const sound_mix& mix, pitched_loop* loop) override;
        void mix(int channel, const sound_mix& mix) override;
        void halt(int channel) override;

    private:
        const audio_engine& engine_;
    };

    static void filter_mix(void* filter_data, unsigned char* stream, int length);
    static void play_pitched(int channel, void* stream, int length, void* loop_data);
    // `sound` heard from `position`, or full and centred with none.
    sound_mix heard(const loaded_chunk& sound, const std::optional<glm::vec3>& position) const;
    void unload_manifest();
    void warn_once(const std::string& key, const std::string& message);

    bool initialized_ = false;
    bool mixer_open_ = false;
    bool filter_registered_ = false;
    int mix_channels_ = 0;
    int filter_sample_rate_ = 0;
    underwater_filter filter_;  // run on the audio thread
    std::unordered_map<std::string, loaded_chunk> chunks_;
    std::unordered_map<std::string, loaded_music> music_;
    sdl_channels channels_{*this};
    voice_pool voices_{channels_};
    std::unordered_set<std::string> warned_;
    std::string active_ambience_;
    audio_levels levels_;
    sound_listener listener_;
    float meters_per_world_unit_ = 1.0f;
};
