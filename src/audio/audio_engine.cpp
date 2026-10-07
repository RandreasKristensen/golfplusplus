#include "audio/audio_engine.h"

#include <SDL.h>
#include <SDL_mixer.h>

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
            chunks_[sound.id] = loaded_chunk{std::move(chunk), mix_gain, sound.type};
        }
    }
    return true;
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
    Mix_PlayChannel(-1, it->second.chunk.get(), 0);
}

void audio_engine::play_loop(const std::string& id) {
    if (!mixer_open_) {
        return;
    }
    const auto active = loop_channels_.find(id);
    if (active != loop_channels_.end() && Mix_Playing(active->second) != 0) {
        return;
    }
    const auto it = chunks_.find(id);
    if (it == chunks_.end()) {
        warn_once("loop:" + id, "audio loop unavailable: " + id);
        return;
    }
    const int channel = Mix_PlayChannel(-1, it->second.chunk.get(), -1);
    if (channel >= 0) {
        loop_channels_[id] = channel;
    }
}

void audio_engine::stop_loop(const std::string& id) {
    const auto it = loop_channels_.find(id);
    if (it == loop_channels_.end()) {
        return;
    }
    if (mixer_open_) {
        Mix_HaltChannel(it->second);
    }
    loop_channels_.erase(it);
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
    loop_channels_.clear();
    active_ambience_.clear();
    chunks_.clear();
    music_.clear();
}

void audio_engine::warn_once(const std::string& key, const std::string& message) {
    if (warned_.insert(key).second) {
        SDL_Log("%s", message.c_str());
    }
}
