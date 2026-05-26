#include "audio/audio_engine.h"

#include <SDL.h>
#include <SDL_mixer.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

bool audio_engine::init() {
    if (initialized_) {
        return mixer_open_;
    }

    initialized_ = true;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        warn_once("sdl_audio_init", std::string("SDL audio init failed: ") + SDL_GetError());
        return false;
    }

    const int flags = MIX_INIT_OGG;
    const int initialized_flags = Mix_Init(flags);
    if ((initialized_flags & flags) != flags) {
        warn_once("mix_init_ogg", std::string("SDL_mixer OGG support unavailable: ") + Mix_GetError());
    }

    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) != 0) {
        warn_once("mix_open_audio", std::string("SDL_mixer open audio failed: ") + Mix_GetError());
        Mix_Quit();
        return false;
    }

    Mix_AllocateChannels(32);
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

    master_volume_ = result.manifest->master_volume;
    category_volumes_ = result.manifest->category_volumes;
    const std::filesystem::path audio_root = manifest_path.parent_path();

    for (const audio_sound_definition& sound : result.manifest->sounds) {
        const std::filesystem::path path = audio_root / std::filesystem::path(sound.file);
        if (!std::filesystem::exists(path)) {
            warn_once("missing:" + path.string(), "audio asset missing: " + path.string());
            continue;
        }

        if (!mixer_open_) {
            continue;
        }

        if (sound.type == audio_sound_type::ambience) {
            Mix_Music* loaded = Mix_LoadMUS(path.string().c_str());
            if (loaded == nullptr) {
                warn_once("load_music:" + path.string(), std::string("audio music load failed: ") + path.string() + " - " + Mix_GetError());
                continue;
            }
            music_[sound.id] = loaded_music{loaded, sound.category, sound.volume_multiplier};
        } else {
            Mix_Chunk* loaded = Mix_LoadWAV(path.string().c_str());
            if (loaded == nullptr) {
                warn_once("load_chunk:" + path.string(), std::string("audio chunk load failed: ") + path.string() + " - " + Mix_GetError());
                continue;
            }
            chunks_[sound.id] = loaded_chunk{loaded, sound.category, sound.volume_multiplier};
        }
    }

    refresh_chunk_volumes();
    return true;
}

void audio_engine::play(const std::string& id) {
    if (!mixer_open_) {
        return;
    }

    const auto it = chunks_.find(id);
    if (it == chunks_.end() || it->second.chunk == nullptr) {
        warn_once("play_missing:" + id, "audio sound unavailable: " + id);
        return;
    }

    Mix_PlayChannel(-1, it->second.chunk, 0);
}

void audio_engine::play_loop(const std::string& id) {
    if (!mixer_open_) {
        return;
    }

    const auto active_it = loop_channels_.find(id);
    if (active_it != loop_channels_.end() && Mix_Playing(active_it->second) != 0) {
        return;
    }

    const auto it = chunks_.find(id);
    if (it == chunks_.end() || it->second.chunk == nullptr) {
        warn_once("loop_missing:" + id, "audio loop unavailable: " + id);
        return;
    }

    const int channel = Mix_PlayChannel(-1, it->second.chunk, -1);
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
    if (it == music_.end() || it->second.music == nullptr) {
        warn_once("ambience_missing:" + id, "audio ambience unavailable: " + id);
        return;
    }

    Mix_VolumeMusic(volume_for_sound(it->second.category, it->second.volume_multiplier));
    if (Mix_PlayMusic(it->second.music, -1) == 0) {
        active_ambience_ = id;
    }
}

void audio_engine::stop_ambience() {
    if (mixer_open_) {
        Mix_HaltMusic();
    }
    active_ambience_.clear();
}

void audio_engine::set_category_volume(const std::string& category, const float volume) {
    category_volumes_[category] = std::max(0.0f, std::min(1.0f, volume));
    refresh_chunk_volumes();

    if (!active_ambience_.empty()) {
        const auto it = music_.find(active_ambience_);
        if (it != music_.end()) {
            Mix_VolumeMusic(volume_for_sound(it->second.category, it->second.volume_multiplier));
        }
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

    for (auto& item : chunks_) {
        if (item.second.chunk != nullptr) {
            Mix_FreeChunk(item.second.chunk);
        }
    }
    chunks_.clear();

    for (auto& item : music_) {
        if (item.second.music != nullptr) {
            Mix_FreeMusic(item.second.music);
        }
    }
    music_.clear();
}

void audio_engine::warn_once(const std::string& key, const std::string& message) {
    if (!warned_.insert(key).second) {
        return;
    }
    std::fprintf(stderr, "%s\n", message.c_str());
}

int audio_engine::volume_for_sound(const std::string& category, const float volume_multiplier) const {
    float category_volume = 1.0f;
    const auto it = category_volumes_.find(category);
    if (it != category_volumes_.end()) {
        category_volume = it->second;
    }

    const float volume = std::max(0.0f, std::min(1.0f, master_volume_ * category_volume * volume_multiplier));
    return static_cast<int>(volume * static_cast<float>(MIX_MAX_VOLUME) + 0.5f);
}

void audio_engine::refresh_chunk_volumes() {
    if (!mixer_open_) {
        return;
    }

    for (auto& item : chunks_) {
        if (item.second.chunk != nullptr) {
            Mix_VolumeChunk(item.second.chunk, volume_for_sound(item.second.category, item.second.volume_multiplier));
        }
    }
}
