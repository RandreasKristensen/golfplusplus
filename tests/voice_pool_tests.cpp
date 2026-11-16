#include "doctest.h"

#include "audio/audio_manifest.h"
#include "audio/sound_ids.h"
#include "audio/sound_placement.h"
#include "audio/voice_pool.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace {
// The mixer's channels as SDL_mixer keeps them: a halted channel's loop is
// never read again; a one-shot plays until finish_one_shots.
class fake_mixer final : public mixer_channels {
public:
    struct channel {
        bool playing = false;
        std::string sound;
        sound_mix mix;
        pitched_loop* loop = nullptr;
    };

    explicit fake_mixer(const int channels) : channels_(static_cast<std::size_t>(channels)) {}

    bool playing(const int channel) const override {
        return at(channel).playing;
    }
    bool start(const int channel, const std::string& sound, const sound_mix& mix, pitched_loop* const loop) override {
        CHECK(!at(channel).playing);
        if (refuse_starts) {
            return false;
        }
        ++starts;
        at(channel) = fake_mixer::channel{true, sound, mix, loop};
        return true;
    }
    void mix(const int channel, const sound_mix& mix) override {
        at(channel).mix = mix;
    }
    void halt(const int channel) override {
        at(channel) = fake_mixer::channel{};
    }

    void finish_one_shots() {
        for (channel& playing : channels_) {
            if (playing.loop == nullptr) {
                playing = channel{};
            }
        }
    }
    std::vector<const channel*> playing_sound(const std::string& sound) const {
        std::vector<const channel*> found;
        for (const channel& playing : channels_) {
            if (playing.playing && playing.sound == sound) {
                found.push_back(&playing);
            }
        }
        return found;
    }
    // What the audio thread reads next: each loop it reads must still be
    // owned by the pool.
    void check_reads(const voice_pool& pool) const {
        for (const channel& playing : channels_) {
            if (playing.loop != nullptr) {
                REQUIRE(pool.holds(playing.loop));
                CHECK(playing.loop->ratio.load() > 0.0f);
            }
        }
    }
    int loops_read() const {
        return static_cast<int>(std::count_if(channels_.begin(), channels_.end(), [](const channel& c) { return c.loop != nullptr; }));
    }

    bool refuse_starts = false;
    int starts = 0;

private:
    // An out-of-range channel fails and reads the last.
    std::size_t index(const int channel) const {
        const bool valid = channel >= 0 && static_cast<std::size_t>(channel) < channels_.size();
        CHECK(valid);
        return valid ? static_cast<std::size_t>(channel) : channels_.size() - 1;
    }
    channel& at(const int channel) {
        return channels_[index(channel)];
    }
    const channel& at(const int channel) const {
        return channels_[index(channel)];
    }

    std::vector<channel> channels_;
};

const audio_manifest& shipped_manifest() {
    static const audio_manifest manifest = *load_audio_manifest_from_file(asset_root() + "/" + sounds_path).manifest;
    return manifest;
}

sound_spread spread_of(const std::string& id) {
    const std::vector<audio_sound_definition>& sounds = shipped_manifest().sounds;
    const auto it = std::find_if(sounds.begin(), sounds.end(), [&id](const audio_sound_definition& s) { return s.id == id; });
    const bool placed = it != sounds.end() && it->spread.has_value();
    CHECK(placed);
    return placed ? *it->spread : sound_spread{};
}

const sound_listener ears = listener_looking(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));

// A sound `meters` to one side of me.
sound_mix heard_at(const std::string& id, const float meters) {
    return place_sound(spread_of(id), glm::vec3(-meters, 0.0f, 0.0f), ears, 1.0f);
}

loop_sound motor(const int owner, const float meters, const float pitch = 1.0f) {
    return loop_sound{"cart_motor:" + std::to_string(owner), sound_cart_drive_loop, heard_at(sound_cart_drive_loop, meters), pitch};
}

float earshot() {
    return spread_of(sound_cart_drive_loop).max;
}

// The shipped manifest's channels and loops' share.
struct shipped_voices {
    fake_mixer mixer{shipped_manifest().voices.channels};
    voice_pool pool{mixer};
    shipped_voices() {
        pool.reset(shipped_manifest().voices.channels, shipped_manifest().voices.loops);
    }
};

std::size_t loop_share() {
    return static_cast<std::size_t>(shipped_manifest().voices.loops);
}

std::size_t one_shot_channels() {
    return static_cast<std::size_t>(shipped_manifest().voices.channels - shipped_manifest().voices.loops);
}
}

TEST_CASE("the shipped voices leave most channels to one-shots") {
    const audio_voices voices = shipped_manifest().voices;
    CHECK(voices.loops >= 1);
    CHECK(voices.channels - voices.loops > voices.loops);
}

TEST_CASE("carts out of earshot hold no channel; one entering earshot starts, leaving stops and returning starts again") {
    shipped_voices audio;
    std::vector<loop_sound> crowd;
    for (int owner = 0; owner < 64; ++owner) {
        crowd.push_back(motor(owner, earshot() * 1.5f + static_cast<float>(owner)));
    }
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == 0);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).empty());

    crowd[5] = motor(5, earshot() * 0.5f);
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == 1);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).size() == 1);
    audio.mixer.check_reads(audio.pool);

    crowd[5] = motor(5, earshot() * 1.01f);
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == 0);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).empty());
    CHECK(audio.mixer.loops_read() == 0);

    crowd[5] = motor(5, earshot() * 0.99f);
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == 1);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).size() == 1);
    audio.mixer.check_reads(audio.pool);
}

TEST_CASE("an audible motor keeps its channel and follows its cart's place and pitch") {
    shipped_voices audio;
    audio.pool.set_loops({motor(0, 5.0f, 1.0f)});
    REQUIRE(audio.mixer.playing_sound(sound_cart_drive_loop).size() == 1);
    const pitched_loop* const started = audio.mixer.playing_sound(sound_cart_drive_loop)[0]->loop;

    const loop_sound moved = motor(0, 40.0f, 1.6f);
    audio.pool.set_loops({moved});
    const std::vector<const fake_mixer::channel*> playing = audio.mixer.playing_sound(sound_cart_drive_loop);
    REQUIRE(playing.size() == 1);
    CHECK(audio.mixer.starts == 1);
    CHECK(playing[0]->loop == started);
    CHECK(near(playing[0]->mix.gain, moved.mix.gain));
    CHECK(near(playing[0]->mix.pan, moved.mix.pan));
    CHECK(moved.mix.gain < 0.5f);
    CHECK(std::abs(moved.mix.pan) > 0.9f);
    CHECK(near(playing[0]->loop->ratio.load(), 1.6f));
    audio.mixer.check_reads(audio.pool);
}

TEST_CASE("a crowd of audible motors plays only the nearest, as many as the loops' share") {
    shipped_voices audio;
    std::vector<loop_sound> crowd;
    const int carts = static_cast<int>(shipped_manifest().voices.channels) + 8;
    for (int owner = 0; owner < carts; ++owner) {
        crowd.push_back(motor(owner, 4.0f + 2.0f * static_cast<float>(carts - owner)));
    }
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == loop_share());
    const std::vector<const fake_mixer::channel*> playing = audio.mixer.playing_sound(sound_cart_drive_loop);
    REQUIRE(playing.size() == loop_share());
    const float quietest_played = (*std::min_element(playing.begin(), playing.end(), [](const auto* a, const auto* b) {
        return a->mix.gain < b->mix.gain;
    }))->mix.gain;
    for (std::size_t skipped = 0; skipped + loop_share() < crowd.size(); ++skipped) {
        CHECK(crowd[skipped].mix.gain < quietest_played);
    }

    // The farthest cart drives up to me: it takes the farthest playing cart's channel.
    crowd[0] = motor(0, 1.0f);
    audio.pool.set_loops(crowd);
    CHECK(audio.pool.loops_held() == loop_share());
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).size() == loop_share());
    bool nearest_playing = false;
    for (const fake_mixer::channel* channel : audio.mixer.playing_sound(sound_cart_drive_loop)) {
        nearest_playing = nearest_playing || near(channel->mix.gain, crowd[0].mix.gain);
    }
    CHECK(nearest_playing);
    audio.mixer.check_reads(audio.pool);
}

TEST_CASE("audible motors filling every channel still leave strikes and the menus heard") {
    shipped_voices audio;
    std::vector<loop_sound> crowd;
    for (int owner = 0; owner < 64; ++owner) {
        crowd.push_back(motor(owner, 3.0f + 0.5f * static_cast<float>(owner)));
    }
    audio.pool.set_loops(crowd);
    for (std::size_t i = 0; i < one_shot_channels(); ++i) {
        audio.pool.play(sound_ball_land_grass, sound_mix{0.05f, 0.0f});
    }
    CHECK(audio.mixer.playing_sound(sound_ball_land_grass).size() == one_shot_channels());
    // Every channel plays: the motors their share, distant landings the rest.
    audio.pool.set_loops(crowd);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).size() == loop_share());

    audio.pool.play(sound_ui_select, sound_mix{});
    CHECK(audio.mixer.playing_sound(sound_ui_select).size() == 1);
    audio.pool.play("club_hit_driver", heard_at("club_hit_driver", 2.0f));
    CHECK(audio.mixer.playing_sound("club_hit_driver").size() == 1);
    // Another distant landing takes no louder sound's channel.
    audio.pool.play(sound_ball_land_grass, sound_mix{0.05f, 0.0f});
    CHECK(audio.mixer.playing_sound(sound_ball_land_grass).size() == one_shot_channels() - 2);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).size() == loop_share());
    audio.mixer.check_reads(audio.pool);

    audio.mixer.finish_one_shots();
    audio.pool.play(sound_ui_back, sound_mix{});
    CHECK(audio.mixer.playing_sound(sound_ui_back).size() == 1);
}

TEST_CASE("a one-shot out of earshot takes no channel") {
    shipped_voices audio;
    audio.pool.play("club_hit_driver", heard_at("club_hit_driver", spread_of("club_hit_driver").max * 2.0f));
    CHECK(audio.mixer.starts == 0);
}

TEST_CASE("churning many carts and reloads keeps loops bounded, and no channel reads a freed loop") {
    shipped_voices audio;
    for (int frame = 0; frame < 400; ++frame) {
        std::vector<loop_sound> carts;
        for (int i = 0; i < 40; ++i) {
            const int owner = frame * 7 + i;  // new account ids every frame
            const float meters = static_cast<float>((owner * 37) % 160);
            carts.push_back(motor(owner, meters, 0.5f + static_cast<float>(owner % 10) * 0.1f));
        }
        carts.push_back(carts.front());  // a repeated key holds one channel
        audio.pool.set_loops(carts);
        CHECK(audio.pool.loops_held() <= loop_share());
        CHECK(audio.mixer.loops_read() == static_cast<int>(audio.pool.loops_held()));
        audio.mixer.check_reads(audio.pool);
        if (frame % 3 == 0) {
            audio.pool.play(sound_ball_land_grass, sound_mix{});
        }
        if (frame % 50 == 49) {
            audio.mixer.finish_one_shots();
            audio.pool.reset(shipped_manifest().voices.channels, shipped_manifest().voices.loops);
            CHECK(audio.pool.loops_held() == 0);
            CHECK(audio.mixer.loops_read() == 0);
        }
    }
    audio.pool.set_loops({});
    CHECK(audio.pool.loops_held() == 0);
    CHECK(audio.mixer.loops_read() == 0);
    CHECK(audio.mixer.playing_sound(sound_cart_drive_loop).empty());
}

TEST_CASE("a loop the mixer cannot start keeps nothing and tries again") {
    shipped_voices audio;
    audio.mixer.refuse_starts = true;
    audio.pool.set_loops({motor(0, 5.0f)});
    CHECK(audio.pool.loops_held() == 0);
    audio.mixer.refuse_starts = false;
    audio.pool.set_loops({motor(0, 5.0f)});
    CHECK(audio.pool.loops_held() == 1);
    audio.mixer.check_reads(audio.pool);
}
