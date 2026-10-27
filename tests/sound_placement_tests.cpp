#include "doctest.h"

#include "audio/audio_manifest.h"
#include "audio/sound_placement.h"
#include "core/render_frame.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "game/remote_players.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace {
constexpr float fov_degrees = 70.0f;

const audio_manifest& shipped_manifest() {
    static const audio_manifest manifest = *load_audio_manifest_from_file(asset_root() + "/" + sounds_path).manifest;
    return manifest;
}

// A placed sound's spread; none (failing) for one not placed.
sound_spread spread_of(const std::string& id) {
    const std::vector<audio_sound_definition>& sounds = shipped_manifest().sounds;
    const auto it = std::find_if(sounds.begin(), sounds.end(), [&id](const audio_sound_definition& s) { return s.id == id; });
    const bool placed = it != sounds.end() && it->spread.has_value();
    CHECK(placed);
    return placed ? *it->spread : sound_spread{};
}

// What I hear from: the camera as the frame shows it.
sound_listener my_ears(const game_state& state) {
    const camera_view view = live_camera_view(state, fov_degrees);
    return listener_looking(view.position, view.target);
}

std::optional<audio_event> first_event(const std::vector<audio_event>& events, const audio_event_type type) {
    const auto it = std::find_if(events.begin(), events.end(), [type](const audio_event& e) { return e.type == type; });
    return it != events.end() ? std::optional<audio_event>(*it) : std::nullopt;
}

float decibels(const float gain) {
    return 20.0f * std::log10(gain);
}
}

TEST_CASE("a sound twice as far is about 6 dB quieter, full up to its reference distance, silent past its maximum") {
    const sound_spread spread{2.0f, 100.0f};
    const sound_listener ears = listener_looking(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    const float near_gain = place_sound(spread, glm::vec3(0.0f, 0.0f, 10.0f), ears, 1.0f).gain;
    const float far_gain = place_sound(spread, glm::vec3(0.0f, 0.0f, 20.0f), ears, 1.0f).gain;
    CHECK(near(decibels(near_gain) - decibels(far_gain), 6.02f, 0.05f));
    CHECK(place_sound(spread, glm::vec3(0.0f, 0.0f, 1.5f), ears, 1.0f).gain == 1.0f);
    CHECK(place_sound(spread, glm::vec3(0.0f, 0.0f, 99.0f), ears, 1.0f).gain > 0.0f);
    CHECK(place_sound(spread, glm::vec3(0.0f, 0.0f, 101.0f), ears, 1.0f).gain == 0.0f);
    // Metres, not world units.
    CHECK(place_sound(spread, glm::vec3(0.0f, 0.0f, 60.0f), ears, 2.0f).gain == 0.0f);
}

TEST_CASE("a sound on my left plays on the left, on my right on the right, ahead or behind in the middle") {
    for (const glm::vec3& forward : {glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(-0.6f, 0.0f, -0.8f)}) {
        const glm::vec3 eye(3.0f, 1.6f, -2.0f);
        const sound_listener ears = listener_looking(eye, eye + forward);
        const sound_spread spread{2.0f, 100.0f};
        const glm::vec3 left = yaw_left(forward);

        const sound_mix on_left = place_sound(spread, eye + left * 10.0f, ears, 1.0f);
        CHECK(near(on_left.pan, -1.0f, 0.001f));
        CHECK(stereo_for(on_left.pan).left == 1.0f);
        CHECK(stereo_for(on_left.pan).right == 0.0f);

        const sound_mix on_right = place_sound(spread, eye - left * 10.0f, ears, 1.0f);
        CHECK(near(on_right.pan, 1.0f, 0.001f));
        CHECK(stereo_for(on_right.pan).left == 0.0f);
        CHECK(stereo_for(on_right.pan).right == 1.0f);

        // Ahead and a little left: mostly centred, leaning left.
        const sound_mix ahead_left = place_sound(spread, eye + forward * 10.0f + left * 2.0f, ears, 1.0f);
        CHECK(ahead_left.pan < 0.0f);
        CHECK(ahead_left.pan > -0.3f);

        for (const float along : {10.0f, -10.0f}) {
            const sound_mix centred = place_sound(spread, eye + forward * along, ears, 1.0f);
            CHECK(near(centred.pan, 0.0f, 0.001f));
            CHECK(near(stereo_for(centred.pan).left, 1.0f, 0.001f));
            CHECK(near(stereo_for(centred.pan).right, 1.0f, 0.001f));
        }
    }
}

TEST_CASE("my own strike is heard at full level, another player's 100 m away far quieter") {
    game_state state = started_hole();
    const glm::vec3 tee = state.ball->position;
    hit_selected_club(state);
    const std::optional<audio_event> strike = first_event(state.audio_events, audio_event_type::club_hit);
    REQUIRE(strike.has_value());
    CHECK(near(strike->position, tee));
    const sound_mix mine = place_sound(spread_of(strike->sound), strike->position, my_ears(state), 1.0f);
    CHECK(mine.gain == 1.0f);

    // Their ball where mine was, and me 100 m off to the side, walking.
    game_state other = started_hole();
    other.play = play_mode::online;
    other.online.status = net_status::connected;
    other.online.account_id = 1;
    shot_setup input;
    input.ball_start = tee;
    input.aim_angle = other.player.yaw;
    input.club_id = other.clubs[other.selected_club].id;
    input = stocked(input, other, 1.0f);
    input.shot_at = shot_unix_ms(other);
    other.player.position = tee + yaw_left(yaw_direction(input.aim_angle)) * 100.0f;
    other.player.yaw = yaw_towards(other.player.position, tee);
    start_remote_shot(other, played_shot(2, 0, 1, input, tee, false));
    const std::optional<audio_event> theirs = first_event(other.audio_events, audio_event_type::club_hit);
    REQUIRE(theirs.has_value());
    CHECK(near(theirs->position, tee));
    const sound_mix heard = place_sound(spread_of(theirs->sound), theirs->position, my_ears(other), 1.0f);
    CHECK(heard.gain > 0.0f);
    CHECK(decibels(heard.gain) < -20.0f);
}

TEST_CASE("a shot's landings are heard on the ground where the ball lands, mine and another player's") {
    game_state state = started_hole();
    still_air(state);
    const glm::vec3 tee = state.ball->position;
    hit_selected_club(state);
    std::vector<audio_event> heard;
    for (int frame = 0; frame < 100000 && state.shot; ++frame) {
        heard.insert(heard.end(), state.audio_events.begin(), state.audio_events.end());
        state.audio_events.clear();
        update_game(state, game_input{}, 0.02f);
    }
    const std::optional<audio_event> landing = first_event(heard, audio_event_type::ball_land);
    REQUIRE(landing.has_value());
    CHECK(horizontal_distance(landing->position, tee) > 5.0f);
    // Where the ball is drawn: its trajectory's points cut the corner of a bounce a little.
    CHECK(near(landing->position.y, terrain_height(state.area, landing->position), 0.5f));

    game_state other = started_hole();
    still_air(other);
    other.play = play_mode::online;
    other.online.status = net_status::connected;
    other.online.account_id = 1;
    shot_setup input;
    input.ball_start = tee;
    input.aim_angle = other.player.yaw;
    input.club_id = other.clubs[other.selected_club].id;
    input = stocked(input, other, 1.0f);
    input.shot_at = shot_unix_ms(other);
    // As their game and the server play it: the server's rest is this one's.
    const shot_course course = make_shot_course(other.area, 0, course_weather_at(other, input.shot_at), other.course,
                                                other.flags_in, other.tuning);
    const shot_result result = simulate_shot(input, course, other.tuning, other.clubs);
    start_remote_shot(other, played_shot(2, 0, 1, input, result.rest_position, false));
    other.audio_events.clear();
    std::vector<audio_event> theirs;
    for (int frame = 0; frame < 100000 && other.remote_shots.count(2) != 0; ++frame) {
        update_remote_players(other, 0.02f);
        theirs.insert(theirs.end(), other.audio_events.begin(), other.audio_events.end());
        other.audio_events.clear();
    }
    const std::optional<audio_event> their_landing = first_event(theirs, audio_event_type::ball_land);
    REQUIRE(their_landing.has_value());
    CHECK(horizontal_distance(their_landing->position, tee) > 5.0f);
    CHECK(near(their_landing->position.y, terrain_height(other.area, their_landing->position), 0.5f));
}

TEST_CASE("menu, HUD, thunder and ambience sounds are never placed; everything else in the world is") {
    for (const audio_sound_definition& sound : shipped_manifest().sounds) {
        const bool unplaced = sound.category == "ui" || sound.category == "ambience";
        CHECK(sound.spread.has_value() == !unplaced);
    }
}

TEST_CASE("a sound must say how far it carries or that it is not placed; ambience is never placed") {
    CHECK(!parse_audio_manifest(R"({ "sounds": [ { "id": "a", "file": "a.wav" } ] })").manifest);
    CHECK(!parse_audio_manifest(R"({ "sounds": [ { "id": "a", "file": "a.wav", "reference_distance": 2.0 } ] })").manifest);
    CHECK(!parse_audio_manifest(
               R"({ "sounds": [ { "id": "a", "file": "a.wav", "reference_distance": 20.0, "max_distance": 10.0 } ] })")
               .manifest);
    CHECK(!parse_audio_manifest(R"({ "sounds": [ { "id": "a", "file": "a.mp3", "type": "ambience",
                                    "reference_distance": 2.0, "max_distance": 10.0 } ] })")
               .manifest);
    const audio_manifest_parse_result unplaced =
        parse_audio_manifest(R"({ "sounds": [ { "id": "a", "file": "a.wav", "placed": false } ] })");
    REQUIRE(unplaced.manifest);
    CHECK(!unplaced.manifest->sounds[0].spread);
}

TEST_CASE("my cart's motor runs from my cart while I drive it, and stops when it stands still") {
    game_state state = started_hole();
    sit_in_cart(state);
    CHECK(running_motors(state).empty());
    game_input throttle;
    throttle.forward_held = true;
    for (int i = 0; i < 50; ++i) {
        update_game(state, throttle, 0.02f);
    }
    const std::vector<running_motor> motors = running_motors(state);
    REQUIRE(motors.size() == 1);
    CHECK(motors[0].owner == 0);
    CHECK(near(motors[0].position, state.cart.pose.position));
    CHECK(motors[0].pitch > state.tuning.cart.motor_idle_pitch);
    // Heard from my seat at full level.
    CHECK(place_sound(spread_of("cart_drive_loop"), motors[0].position, my_ears(state), 1.0f).gain == 1.0f);
    for (int i = 0; i < 500 && std::abs(my_cart_speed(state)) > 0.0f; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(running_motors(state).empty());
}
