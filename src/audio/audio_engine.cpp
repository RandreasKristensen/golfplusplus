#include "audio/audio_engine.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <cmath>
#include <system_error>

namespace {
constexpr int mixer_frequency = 44100;
constexpr int mixer_channels = 2;
constexpr int mixer_chunk_size = 1024;
constexpr int mixer_voices = 32;

int mixer_volume(const float gain) {
    return static_cast<int>(gain * static_cast<float>(MIX_MAX_VOLUME) + 0.5f);
}
}

void audio_engine::chunk_deleter::operator()(Mix_Chunk* chunk) const {
    Mix_FreeChunk(chunk);
}

void audio_engine::music_deleter::operator()(Mix_Music* music) const {
    Mix_FreeMusic(music);
}

audio_engine::~audio_engine() {
    shutdown();
}

bool audio_engine::init() {
    if (initialized_) {
        return mixer_open_;
    }

    initialized_ = true;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        warn_once("sdl_audio_init", std::string("SDL audio init failed: ") + SDL_GetError());
        return false;
    }

    if ((Mix_Init(MIX_INIT_OGG) & MIX_INIT_OGG) != MIX_INIT_OGG) {
        warn_once("mix_init_ogg", std::string("SDL_mixer OGG support unavailable: ") + Mix_GetError());
    }

    if (Mix_OpenAudio(mixer_frequency, MIX_DEFAULT_FORMAT, mixer_channels, mixer_chunk_size) != 0) {
        warn_once("mix_open_audio", std::string("SDL_mixer open audio failed: ") + Mix_GetError());
        Mix_Quit();
        return false;
    }

    Mix_AllocateChannels(mixer_voices);
    mixer_open_ = true;
    return true;
}

bool audio_engine::load_manifest(const std::filesystem::path& manifest_path) {
    unload_manifest();

    const audio_manifest_parse_result result = load_audio_manifest_from_file(manifest_path.string());
    if (!result.manifest) {
        warn_once("manifest:" + manifest_path.string(), result.error);
        return false;
    }

    const std::filesystem::path audio_root = manifest_path.parent_path();
    for (const audio_sound_definition& sound : result.manifest->sounds) {
        const std::filesystem::path path = audio_root / sound.file;
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            warn_once("missing:" + path.string(), "audio asset missing: " + path.string());
            continue;
        }
        if (!mixer_open_) {
            continue;
        }

        const float mix_gain = sound_mix_gain(*result.manifest, sound);
        if (sound.type == audio_sound_type::ambience) {
            std::unique_ptr<Mix_Music, music_deleter> music(Mix_LoadMUS(path.string().c_str()));
            if (!music) {
                warn_once("load:" + path.string(), "audio load failed: " + path.string() + " - " + Mix_GetError());
                continue;
            }
            music_[sound.id] = loaded_music{std::move(music), mix_gain};
        } else {
            std::unique_ptr<Mix_Chunk, chunk_deleter> chunk(Mix_LoadWAV(path.string().c_str()));
            if (!chunk) {
                warn_once("load:" + path.string(), "audio load failed: " + path.string() + " - " + Mix_GetError());
                continue;
            }
            Mix_VolumeChunk(chunk.get(), mixer_volume(played_gain(mix_gain, sound.type, levels_)));
            chunks_[sound.id] = loaded_chunk{std::move(chunk), mix_gain, sound.type, sound.spread};
        }
    }
    return true;
}

void audio_engine::set_listener(const sound_listener& listener, const float meters_per_world_unit) {
    listener_ = listener;
    meters_per_world_unit_ = meters_per_world_unit;
}

// The channel's volume multiplies the chunk's (the mix and the player's
// levels); its panning is the mixer's own effect, after a pitched loop's.
void audio_engine::mix_channel(const int channel, const loaded_chunk& sound, const std::optional<glm::vec3>& position) const {
    sound_mix mix;
    if (position && sound.spread) {
        mix = place_sound(*sound.spread, *position, listener_, meters_per_world_unit_);
    }
    const stereo_gains sides = stereo_for(mix.pan);
    Mix_Volume(channel, mixer_volume(mix.gain));
    Mix_SetPanning(channel, static_cast<Uint8>(sides.left * 255.0f + 0.5f), static_cast<Uint8>(sides.right * 255.0f + 0.5f));
}

// Mixed before it starts, so its first samples play at their level.
std::optional<int> audio_engine::start(const loaded_chunk& sound, const std::optional<glm::vec3>& position, const int loops) const {
    const int channel = Mix_GroupAvailable(-1);
    if (channel < 0) {
        return std::nullopt;
    }
    mix_channel(channel, sound, position);
    if (Mix_PlayChannel(channel, sound.chunk.get(), loops) < 0) {
        return std::nullopt;
    }
    return channel;
}

void audio_engine::play(const std::string& id) {
    if (!mixer_open_) {
        return;
    }
    const auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        warn_once("play:" + id, "audio sound unavailable: " + id);
        return;
    }
    start(it->second, std::nullopt, 0);
}

void audio_engine::play_at(const std::string& id, const glm::vec3& position) {
    if (!mixer_open_) {
        return;
    }
    const auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        warn_once("play:" + id, "audio sound unavailable: " + id);
        return;
    }
    // Out of earshot it is not played at all.
    if (it->second.spread && place_sound(*it->second.spread, position, listener_, meters_per_world_unit_).gain <= 0.0f) {
        return;
    }
    start(it->second, position, 0);
}

// The mixer's effect for a pitched loop (16-bit stereo, as the mixer opens):
// the channel's own samples replaced by its chunk read at the loop's rate.
// Runs on the audio thread.
void audio_engine::play_pitched(int, void* stream, const int length, void* loop_data) {
    auto& loop = *static_cast<pitched_loop*>(loop_data);
    const auto* in = reinterpret_cast<const Sint16*>(loop.chunk->abuf);
    auto* out = static_cast<Sint16*>(stream);
    const std::size_t frames_in = loop.chunk->alen / (2 * sizeof(Sint16));
    const int frames_out = length / static_cast<int>(2 * sizeof(Sint16));
    if (frames_in < 2) {
        return;
    }
    const double ratio = static_cast<double>(loop.ratio.load());
    double position = loop.position;
    for (int frame = 0; frame < frames_out; ++frame) {
        const std::size_t at = static_cast<std::size_t>(position);
        const std::size_t next = (at + 1) % frames_in;
        const double between = position - static_cast<double>(at);
        for (std::size_t side = 0; side < 2; ++side) {
            const double sample = in[at * 2 + side] * (1.0 - between) + in[next * 2 + side] * between;
            out[static_cast<std::size_t>(frame) * 2 + side] = static_cast<Sint16>(sample);
        }
        position = std::fmod(position + ratio, static_cast<double>(frames_in));
    }
    loop.position = position;
}

void audio_engine::play_loop(const std::string& key, const std::string& id, const glm::vec3& position) {
    if (!mixer_open_) {
        return;
    }
    const auto active = loops_.find(key);
    if (active != loops_.end() && Mix_Playing(active->second.channel) != 0) {
        return;
    }
    const auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        warn_once("loop:" + id, "audio loop unavailable: " + id);
        return;
    }
    const std::optional<int> channel = start(it->second, position, -1);
    if (!channel) {
        return;
    }
    loops_[key] = playing_loop{*channel, id};
    int frequency = 0;
    Uint16 format = 0;
    int channels = 0;
    if (Mix_QuerySpec(&frequency, &format, &channels) == 0 || format != AUDIO_S16SYS || channels != 2) {
        return;
    }
    std::unique_ptr<pitched_loop>& loop = pitched_loops_[key];
    if (!loop) {
        loop = std::make_unique<pitched_loop>();
    }
    loop->chunk = it->second.chunk.get();
    loop->position = 0.0;
    loop->ratio.store(1.0f);
    // The pitched effect rewrites the channel's samples, so the panning comes
    // off and goes back on after it.
    Mix_SetPanning(*channel, 255, 255);
    Mix_RegisterEffect(*channel, play_pitched, nullptr, loop.get());
    mix_channel(*channel, it->second, position);
}

void audio_engine::place_loop(const std::string& key, const glm::vec3& position) {
    const auto loop = loops_.find(key);
    if (!mixer_open_ || loop == loops_.end()) {
        return;
    }
    const auto it = chunks_.find(loop->second.id);
    if (it != chunks_.end()) {
        mix_channel(loop->second.channel, it->second, position);
    }
}

void audio_engine::set_loop_pitch(const std::string& key, const float ratio) {
    const auto loop = pitched_loops_.find(key);
    if (loop != pitched_loops_.end() && std::isfinite(ratio) && ratio > 0.0f) {
        loop->second->ratio.store(ratio);
    }
}

void audio_engine::stop_loop(const std::string& key) {
    const auto it = loops_.find(key);
    if (it == loops_.end()) {
        return;
    }
    if (mixer_open_) {
        Mix_HaltChannel(it->second.channel);
    }
    loops_.erase(it);
}

void audio_engine::start_ambience(const std::string& id) {
    if (!mixer_open_ || active_ambience_ == id) {
        return;
    }
    Mix_HaltMusic();
    active_ambience_.clear();

    const auto it = music_.find(id);
    if (it == music_.end()) {
        warn_once("ambience:" + id, "audio ambience unavailable: " + id);
        return;
    }
    Mix_VolumeMusic(mixer_volume(played_gain(it->second.mix_gain, audio_sound_type::ambience, levels_)));
    if (Mix_PlayMusic(it->second.music.get(), -1) == 0) {
        active_ambience_ = id;
    }
}

// SDL_mixer reads a chunk's volume while mixing, so a changed chunk volume
// reaches the channels already playing it, loops included.
void audio_engine::set_levels(const audio_levels& levels) {
    levels_ = levels;
    if (!mixer_open_) {
        return;
    }
    for (const auto& entry : chunks_) {
        const loaded_chunk& loaded = entry.second;
        Mix_VolumeChunk(loaded.chunk.get(), mixer_volume(played_gain(loaded.mix_gain, loaded.type, levels_)));
    }
    const auto it = music_.find(active_ambience_);
    if (it != music_.end()) {
        Mix_VolumeMusic(mixer_volume(played_gain(it->second.mix_gain, audio_sound_type::ambience, levels_)));
    }
}

void audio_engine::shutdown() {
    unload_manifest();
    if (mixer_open_) {
        Mix_CloseAudio();
        mixer_open_ = false;
    }
    if (initialized_) {
        Mix_Quit();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        initialized_ = false;
    }
}

void audio_engine::unload_manifest() {
    if (mixer_open_) {
        Mix_HaltChannel(-1);
        Mix_HaltMusic();
    }
    loops_.clear();
    active_ambience_.clear();
    chunks_.clear();
    music_.clear();
}

void audio_engine::warn_once(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) {
        SDL_Log("%s", message.c_str());
    }
}
