#include "audio/voice_pool.h"

#include <algorithm>
#include <cmath>

voice_pool::voice_pool(mixer_channels& mixer) : mixer_(mixer) {}

void voice_pool::reset(const int channels, const int loops) {
    halt_all();
    voices_.assign(static_cast<std::size_t>(std::max(channels, 0)), voice{});
    loop_share_ = static_cast<std::size_t>(std::clamp(loops, 0, std::max(channels, 0)));
}

int voice_pool::idle_channel() const {
    for (std::size_t channel = 0; channel < voices_.size(); ++channel) {
        if (voices_[channel].loop.empty() && !mixer_.playing(static_cast<int>(channel))) {
            return static_cast<int>(channel);
        }
    }
    return -1;
}

void voice_pool::play(const std::string& sound, const sound_mix& mix) {
    if (mix.gain <= 0.0f) {
        return;
    }
    int channel = idle_channel();
    if (channel < 0) {
        float quietest = mix.gain;
        for (std::size_t at = 0; at < voices_.size(); ++at) {
            if (voices_[at].loop.empty() && voices_[at].level < quietest) {
                quietest = voices_[at].level;
                channel = static_cast<int>(at);
            }
        }
        if (channel < 0) {
            return;
        }
        mixer_.halt(channel);
    }
    voices_[static_cast<std::size_t>(channel)] = voice{};
    if (mixer_.start(channel, sound, mix, nullptr)) {
        voices_[static_cast<std::size_t>(channel)].level = mix.gain;
    }
}

void voice_pool::release(const held_loop& loop) {
    mixer_.halt(loop.channel);
    voices_[static_cast<std::size_t>(loop.channel)] = voice{};
}

void voice_pool::set_loops(const std::vector<loop_sound>& loops) {
    // The audible loops, loudest first, as many as the loops' share.
    std::vector<const loop_sound*> heard;
    for (const loop_sound& loop : loops) {
        const bool listed = std::any_of(heard.begin(), heard.end(), [&loop](const loop_sound* other) { return other->key == loop.key; });
        if (loop.mix.gain > 0.0f && !listed) {
            heard.push_back(&loop);
        }
    }
    std::stable_sort(heard.begin(), heard.end(),
                     [](const loop_sound* a, const loop_sound* b) { return a->mix.gain > b->mix.gain; });
    heard.resize(std::min(heard.size(), loop_share_));

    for (auto it = loops_.begin(); it != loops_.end();) {
        const auto kept = std::find_if(heard.begin(), heard.end(), [&it](const loop_sound* loop) {
            return loop->key == it->first && loop->sound == it->second.sound;
        });
        if (kept == heard.end() || !mixer_.playing(it->second.channel)) {
            release(it->second);
            it = loops_.erase(it);
        } else {
            ++it;
        }
    }

    for (const loop_sound* loop : heard) {
        const float pitch = std::isfinite(loop->pitch) && loop->pitch > 0.0f ? loop->pitch : 1.0f;
        const auto held = loops_.find(loop->key);
        if (held != loops_.end()) {
            mixer_.mix(held->second.channel, loop->mix);
            held->second.pitch->ratio.store(pitch);
            continue;
        }
        const int channel = idle_channel();
        if (channel < 0) {
            break;
        }
        auto state = std::make_unique<pitched_loop>();
        state->ratio.store(pitch);
        if (!mixer_.start(channel, loop->sound, loop->mix, state.get())) {
            continue;
        }
        voices_[static_cast<std::size_t>(channel)] = voice{loop->key, 0.0f};
        loops_.emplace(loop->key, held_loop{channel, loop->sound, std::move(state)});
    }
}

void voice_pool::halt_all() {
    for (std::size_t channel = 0; channel < voices_.size(); ++channel) {
        mixer_.halt(static_cast<int>(channel));
        voices_[channel] = voice{};
    }
    loops_.clear();
}

std::size_t voice_pool::loops_held() const {
    return loops_.size();
}

bool voice_pool::holds(const pitched_loop* loop) const {
    return std::any_of(loops_.begin(), loops_.end(),
                       [loop](const auto& held) { return held.second.pitch.get() == loop; });
}
