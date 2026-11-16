#include "audio/audio_engine.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <cmath>
#include <system_error>

namespace {
constexpr int mixer_frequency = 44100;
constexpr int mixer_outputs = 2;  // stereo
constexpr int mixer_chunk_size = 1024;

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

    if (Mix_OpenAudio(mixer_frequency, MIX_DEFAULT_FORMAT, mixer_outputs, mixer_chunk_size) != 0) {
        warn_once("mix_open_audio", std::string("SDL_mixer open audio failed: ") + Mix_GetError());
        Mix_Quit();
        return false;
    }

    mixer_open_ = true;
    int frequency = 0;
    Uint16 format = 0;
    if (Mix_QuerySpec(&frequency, &format, &mix_channels_) == 0 || format != AUDIO_S16SYS) {
        warn_once("mix_format", "audio output is not 16-bit; the underwater muffling is off");
    } else {
        // Configured under the audio lock once the manifest is loaded; until
        // then the filter passes everything through.
        filter_sample_rate_ = frequency;
        Mix_SetPostMix(filter_mix, this);
        filter_registered_ = true;
    }
    return true;
}

bool audio_engine::load_manifest(const std::filesystem::path& manifest_path) {
    unload_manifest();

    const audio_manifest_parse_result result = load_audio_manifest_from_file(manifest_path.string());
    if (!result.manifest) {
        warn_once("manifest:" + manifest_path.string(), result.error);
        return false;
    }

    if (mixer_open_) {
        const int channels = Mix_AllocateChannels(result.manifest->voices.channels);
        voices_.reset(channels, result.manifest->voices.loops);
    }

    if (filter_registered_) {
        SDL_LockAudio();
        filter_.configure(result.manifest->underwater, filter_sample_rate_);
        SDL_UnlockAudio();
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
            // A sound with no samples: SDL_mixer may already have freed its
            // buffer (a realloc to 0 bytes) yet still points at it, so only
            // the chunk itself is freed.
            if (chunk->alen == 0) {
                SDL_free(chunk.release());
                warn_once("load:" + path.string(), "audio load failed: " + path.string() + " has no sound in it");
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

sound_mix audio_engine::heard(const loaded_chunk& sound, const std::optional<glm::vec3>& position) const {
    return position && sound.spread ? place_sound(*sound.spread, *position, listener_, meters_per_world_unit_) : sound_mix{};
}

bool audio_engine::sdl_channels::playing(const int channel) const {
    return Mix_Playing(channel) != 0;
}

// Mixed before it starts, so its first samples play at their level. A
// pitched loop's effect rewrites the channel's samples, so the panning (the
// mixer's own effect, run in the order registered) comes off and goes back on
// after it. The effect goes on only once the channel plays: SDL_mixer drops a
// channel's effects when it stops or is halted while playing, never an idle
// channel's.
bool audio_engine::sdl_channels::start(const int channel, const std::string& sound, const sound_mix& mix,
                                       pitched_loop* const loop) {
    const auto it = engine_.chunks_.find(sound);
    if (it == engine_.chunks_.end()) {
        return false;
    }
    Mix_Chunk* const chunk = it->second.chunk.get();
    this->mix(channel, mix);
    if (Mix_PlayChannel(channel, chunk, loop != nullptr ? -1 : 0) < 0) {
        return false;
    }
    int frequency = 0;
    Uint16 format = 0;
    int channels = 0;
    if (loop == nullptr || Mix_QuerySpec(&frequency, &format, &channels) == 0 || format != AUDIO_S16SYS || channels != 2) {
        return true;
    }
    loop->chunk = chunk;
    Mix_SetPanning(channel, 255, 255);
    Mix_RegisterEffect(channel, play_pitched, nullptr, loop);
    this->mix(channel, mix);
    return true;
}

// The channel's volume multiplies the chunk's (the mix and the player's levels).
void audio_engine::sdl_channels::mix(const int channel, const sound_mix& mix) {
    const stereo_gains sides = stereo_for(mix.pan);
    Mix_Volume(channel, mixer_volume(mix.gain));
    Mix_SetPanning(channel, static_cast<Uint8>(sides.left * 255.0f + 0.5f), static_cast<Uint8>(sides.right * 255.0f + 0.5f));
}

// Mix_HaltChannel and Mix_UnregisterEffect each hold the audio device's lock,
// which the audio thread holds while it mixes, so once they return the
// channel's effects are gone and its loop is no longer read.
void audio_engine::sdl_channels::halt(const int channel) {
    Mix_HaltChannel(channel);
    Mix_UnregisterEffect(channel, play_pitched);
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
    voices_.play(id, sound_mix{});
}

// Out of earshot it is not played at all.
void audio_engine::play_at(const std::string& id, const glm::vec3& position) {
    if (!mixer_open_) {
        return;
    }
    const auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        warn_once("play:" + id, "audio sound unavailable: " + id);
        return;
    }
    voices_.play(id, heard(it->second, position));
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

void audio_engine::set_loops(const std::vector<loop_request>& loops) {
    if (!mixer_open_) {
        return;
    }
    std::vector<loop_sound> sounds;
    for (const loop_request& loop : loops) {
        const auto it = chunks_.find(loop.sound);
        if (it == chunks_.end()) {
            warn_once("loop:" + loop.sound, "audio loop unavailable: " + loop.sound);
            continue;
        }
        sounds.push_back(loop_sound{loop.key, loop.sound, heard(it->second, loop.position), loop.pitch});
    }
    voices_.set_loops(sounds);
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

void audio_engine::set_underwater(const bool underwater) {
    filter_.set_target(underwater);
}

// The mixer's post-mix: the whole mix, music included, on the audio thread.
void audio_engine::filter_mix(void* const filter_data, unsigned char* const stream, const int length) {
    auto& engine = *static_cast<audio_engine*>(filter_data);
    const int frames = length / (static_cast<int>(sizeof(Sint16)) * engine.mix_channels_);
    engine.filter_.process(reinterpret_cast<Sint16*>(stream), frames, engine.mix_channels_);
}

void audio_engine::shutdown() {
    unload_manifest();
    if (filter_registered_) {
        Mix_SetPostMix(nullptr, nullptr);
        filter_registered_ = false;
    }
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

// Every channel halts before the chunks they play and their loops are freed.
void audio_engine::unload_manifest() {
    if (mixer_open_) {
        voices_.halt_all();
        Mix_HaltMusic();
    }
    active_ambience_.clear();
    chunks_.clear();
    music_.clear();
}

void audio_engine::warn_once(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) {
        SDL_Log("%s", message.c_str());
    }
}
