#pragma once

// Who holds the mixer's channels. Loops hold at most the manifest's share
// (audio_voices), the loudest first and none out of earshot, so a crowd of
// carts leaves the rest free for strikes and the menus; a loop gets a channel
// again when it is heard again. A one-shot sound with no free channel takes
// the quietest playing one-shot's, if it is louder. The mixer is behind
// mixer_channels: SDL_mixer in the game (audio_engine.cpp), a fake in tests.

#include "audio/sound_placement.h"

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct Mix_Chunk;

// A loop played back at its own rate: the mixer's effect reads `chunk` from
// `position` (frames) on, stepping `ratio` frames each frame, on the audio
// thread. voice_pool owns it while the loop holds its channel.
struct pitched_loop {
    const Mix_Chunk* chunk = nullptr;
    double position = 0.0;
    std::atomic<float> ratio{1.0f};
};

class mixer_channels {
public:
    virtual ~mixer_channels() = default;
    virtual bool playing(int channel) const = 0;
    // Starts `sound` on the idle `channel` heard as `mix`: once, or with a
    // `loop` until halted, reading it. False if it did not start, and then
    // the mixer does not read `loop`.
    virtual bool start(int channel, const std::string& sound, const sound_mix& mix, pitched_loop* loop) = 0;
    virtual void mix(int channel, const sound_mix& mix) = 0;
    // Stops `channel`; once it returns the mixer never reads its loop again.
    virtual void halt(int channel) = 0;
};

// A loop heard now as `mix`, played at `pitch` times its rate.
struct loop_sound {
    std::string key;
    std::string sound;
    sound_mix mix;
    float pitch = 1.0f;
};

class voice_pool {
public:
    explicit voice_pool(mixer_channels& mixer);
    voice_pool(const voice_pool&) = delete;
    voice_pool& operator=(const voice_pool&) = delete;

    // Halts every channel and shares `channels` out anew, at most `loops` of
    // them to loops.
    void reset(int channels, int loops);
    // Plays `sound` once as `mix`; silent ones take no channel.
    void play(const std::string& sound, const sound_mix& mix);
    // The loops heard now: each listed starts, keeps playing (moved and
    // repitched) or stops as its level and the loops' share allow; any loop
    // not listed stops.
    void set_loops(const std::vector<loop_sound>& loops);
    void halt_all();

    std::size_t loops_held() const;
    bool holds(const pitched_loop* loop) const;

private:
    struct voice {
        std::string loop;   // the key of the loop holding it, or empty
        float level = 0.0f; // a one-shot's placed level when it started
    };
    struct held_loop {
        int channel = -1;
        std::string sound;
        std::unique_ptr<pitched_loop> pitch;
    };

    // A channel neither playing nor a loop's: -1 if none.
    int idle_channel() const;
    // Halts the loop's channel, after which its pitch can be freed.
    void release(const held_loop& loop);

    mixer_channels& mixer_;
    std::vector<voice> voices_;
    std::size_t loop_share_ = 0;
    std::unordered_map<std::string, held_loop> loops_;
};
