#include "doctest.h"
#include "core/render_frame.h"
#include "game/progress_rules.h"
#include "game/rangefinder.h"
#include "game/sight_line.h"
#include "game/weather.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
rangefinder_tuning test_rangefinder() {
    rangefinder_tuning tuning;
    tuning.magnification = 3.0f;
    tuning.raise_seconds = 0.15f;
    tuning.max_range_meters = 600.0f;
    tuning.march_step_meters = 2.0f;
    tuning.refine_steps = 12;
    tuning.flag_lock_degrees = 3.0f;
    return tuning;
}

// Level ground at height 0 from z = 0 to 300.
play_area flat_area(const game_tuning& tuning) {
    return hole_area(straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 40.0f), tuning);
}

// What it sees: `flags` 2 units tall, and what stands on `area`.
rangefinder_view view_of(const std::vector<glm::vec3>& flags = {}, const play_area* area = nullptr) {
    rangefinder_view view;
    view.flags = flags;
    view.flag_height = 2.0f;
    view.scene.area = area;
    return view;
}

// Straight down +Z, `pitch_degrees` up.
glm::vec3 look_along_z(const float pitch_degrees) {
    const float pitch = glm::radians(pitch_degrees);
    return yaw_direction(0.0f) * std::cos(pitch) + world_up * std::sin(pitch);
}

// A hole started with the weather held clear, so the rangefinder reads true.
game_state dry_hole() {
    game_state state = started_hole();
    state.weather_override = with_weather_kind(state.weather, weather_kind::clear, state.tuning.weather);
    return state;
}

// Held up for long enough to zoom all the way.
void raise_rangefinder(game_state& state) {
    game_input input;
    input.rangefinder_held = true;
    for (int i = 0; i < 20; ++i) {
        update_game(state, input, 0.05f);
    }
}
}

TEST_CASE("the rangefinder reads the level distance to the ground at its reticle") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area area = flat_area(tuning);
    const glm::vec3 player(0.0f, 0.0f, 100.0f);
    REQUIRE(near(terrain_height(area, glm::vec3(0.0f, 0.0f, 120.0f)), 0.0f, 0.001f));
    REQUIRE(near(terrain_height(area, glm::vec3(0.0f, 0.0f, 160.0f)), 0.0f, 0.001f));

    const float eye_height = 1.65f;
    const glm::vec3 eye = player + world_up * eye_height;
    const float pitch = -3.0f;
    const rangefinder_reading reading =
        read_rangefinder(area, view_of(), player, eye, look_along_z(pitch), test_rangefinder(), tuning.scale);
    REQUIRE(reading.distance_meters.has_value());
    CHECK(near(*reading.distance_meters, eye_height / std::tan(glm::radians(-pitch)), 0.05f));
    CHECK(!reading.flag);
}

TEST_CASE("the rangefinder reads nothing at the sky or beyond its range") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area area = flat_area(tuning);
    const glm::vec3 player(0.0f, 0.0f, 100.0f);
    const glm::vec3 eye = player + world_up * 1.65f;
    CHECK(!read_rangefinder(area, view_of(), player, eye, look_along_z(10.0f), test_rangefinder(), tuning.scale)
               .distance_meters);

    rangefinder_tuning short_range = test_rangefinder();
    short_range.max_range_meters = 10.0f;
    CHECK(!ground_hit(area, eye, look_along_z(-3.0f), short_range, tuning.scale));
}

TEST_CASE("the rangefinder locks onto the flag near its reticle") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area area = flat_area(tuning);
    const glm::vec3 player(0.0f, 0.0f, 100.0f);
    const glm::vec3 eye = player + world_up * 1.65f;
    const glm::vec3 flag(0.0f, 0.0f, 250.0f);
    const auto aimed = [&](const float yaw_degrees, const float pitch_degrees) {
        const float pitch = glm::radians(pitch_degrees);
        const glm::vec3 look = yaw_direction(glm::radians(yaw_degrees)) * std::cos(pitch) + world_up * std::sin(pitch);
        return read_rangefinder(area, view_of({flag}), player, eye, look, test_rangefinder(), tuning.scale);
    };

    // Just beside the stick, and just over its top: the flag.
    for (const rangefinder_reading& reading : {aimed(2.0f, 0.0f), aimed(0.0f, 1.0f)}) {
        REQUIRE(reading.flag.has_value());
        CHECK(near(*reading.flag, flag));
        REQUIRE(reading.distance_meters.has_value());
        CHECK(near(*reading.distance_meters, 150.0f, 0.001f));
    }
    // Further off it reads the ground in front instead.
    const rangefinder_reading ground = aimed(6.0f, -2.0f);
    CHECK(!ground.flag);
    REQUIRE(ground.distance_meters.has_value());
    CHECK(*ground.distance_meters < 60.0f);
}

TEST_CASE("the rangefinder is an item everyone owns today") {
    CHECK(owns_rangefinder(save_data{}));
}

TEST_CASE("held up the rangefinder magnifies its eyepiece, not the view, and slows the look") {
    game_state state = dry_hole();
    const float fov = 70.0f;
    CHECK(near(rangefinder_magnification(state), 1.0f));

    raise_rangefinder(state);
    REQUIRE(state.rangefinder_active);
    CHECK(near(rangefinder_magnification(state), state.tuning.rangefinder.magnification));
    CHECK(near(live_camera_view(state, fov).fov_degrees, fov));
    const render_data data = make_render_data(state, shipped_text_assets(), shipped_content().skills, render_meshes{}, test_props(), fov, false);
    CHECK(near(data.rangefinder_eyepiece.magnification, state.tuning.rangefinder.magnification));

    const float yaw = state.player.yaw;
    game_input look;
    look.rangefinder_held = true;
    look.look_yaw = 0.3f;
    update_game(state, look, 0.016f);
    CHECK(near(wrap_angle(state.player.yaw - yaw), 0.3f / state.tuning.rangefinder.magnification, 0.0001f));

    for (int i = 0; i < 20; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(!state.rangefinder_active);
    CHECK(near(rangefinder_magnification(state), 1.0f));
}

TEST_CASE("the rangefinder reads what it points at as above or below the ground under me") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area slope = hole_area(straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 30.0f, 300.0f), 40.0f), tuning);
    const glm::vec3 player(0.0f, terrain_height(slope, glm::vec3(0.0f, 0.0f, 150.0f)), 150.0f);
    REQUIRE(terrain_height(slope, glm::vec3(0.0f, 0.0f, 200.0f)) > player.y + 1.0f);
    REQUIRE(terrain_height(slope, glm::vec3(0.0f, 0.0f, 100.0f)) < player.y - 1.0f);
    const glm::vec3 eye = player + world_up * 1.65f;
    const auto aimed = [&](const float yaw_degrees, const float pitch_degrees) {
        const float pitch = glm::radians(pitch_degrees);
        const glm::vec3 look = yaw_direction(glm::radians(yaw_degrees)) * std::cos(pitch) + world_up * std::sin(pitch);
        return read_rangefinder(slope, view_of(), player, eye, look, test_rangefinder(), tuning.scale);
    };

    const rangefinder_reading uphill = aimed(0.0f, 0.0f);
    REQUIRE(uphill.height_meters.has_value());
    CHECK(*uphill.height_meters > 0.5f);
    const rangefinder_reading downhill = aimed(180.0f, -12.0f);
    REQUIRE(downhill.height_meters.has_value());
    CHECK(*downhill.height_meters < -0.5f);
    CHECK(!aimed(0.0f, 30.0f).height_meters);

    // Locked onto a flag it is the flag's foot over my ground.
    const glm::vec3 flag(0.0f, terrain_height(slope, glm::vec3(0.0f, 0.0f, 200.0f)), 200.0f);
    const float pitch = std::atan2(flag.y + 1.0f - eye.y, 50.0f);
    const glm::vec3 look = yaw_direction(0.0f) * std::cos(pitch) + world_up * std::sin(pitch);
    const rangefinder_reading on_flag =
        read_rangefinder(slope, view_of({flag}), player, eye, look, test_rangefinder(), tuning.scale);
    REQUIRE(on_flag.flag.has_value());
    CHECK(near(*on_flag.height_meters, (flag.y - player.y) * tuning.scale.meters_per_world_unit, 0.001f));
}

TEST_CASE("the rangefinder's frame signs the height up or down hill") {
    game_state state = dry_hole();
    raise_rangefinder(state);
    const auto label = [&](const float meters) {
        state.rangefinder.height_meters = meters;
        return make_render_data(state, shipped_text_assets(), shipped_content().skills, render_meshes{}, test_props(), 70.0f, false)
            .rangefinder_height_label;
    };
    CHECK(label(4.4f) == "+4M");
    CHECK(label(-6.6f) == "-7M");
    CHECK(label(0.3f) == "0M");
    CHECK(label(-0.3f) == "0M");
}

TEST_CASE("the rangefinder's flag reading shrinks walking towards the pin") {
    game_state state = dry_hole();
    const glm::vec3 pin = pin_anchor_position(state, state.ball->hole);
    const glm::vec3 eye = first_person_eye(state);
    const glm::vec3 to_pin = pin + world_up - eye;
    state.player.yaw = yaw_towards(state.player.position, pin);
    state.look.pitch = std::atan2(to_pin.y, glm::length(horizontal(to_pin)));
    raise_rangefinder(state);
    REQUIRE(state.rangefinder.flag.has_value());
    REQUIRE(state.rangefinder.distance_meters.has_value());
    const float before = *state.rangefinder.distance_meters;
    CHECK(rangefinder_lock_lit(state));
    CHECK(near(before, horizontal_distance(state.player.position, pin) * state.tuning.scale.meters_per_world_unit, 0.001f));

    game_input input;
    input.rangefinder_held = true;
    input.forward_held = true;
    update_game(state, input, 0.05f);
    REQUIRE(state.rangefinder.distance_meters.has_value());
    CHECK(*state.rangefinder.distance_meters < before);

    CHECK(rounded_rangefinder_meters(124.49f) == 124);
    CHECK(rounded_rangefinder_meters(124.50f) == 125);
    CHECK(rounded_rangefinder_meters(-3.0f) == 0);
}

TEST_CASE("the rangefinder's frame shows the wind across the view") {
    game_state state = dry_hole();
    raise_rangefinder(state);
    state.weather.wind = yaw_direction(state.player.yaw + glm::radians(90.0f)) * 4.5f;
    const render_data data = make_render_data(state, shipped_text_assets(), shipped_content().skills, render_meshes{}, test_props(), 70.0f, false);
    REQUIRE(data.show_rangefinder);
    CHECK(data.rangefinder_wind_label == "4.5 M/S");
    REQUIRE(data.rangefinder_wind_angle.has_value());
    CHECK(near(*data.rangefinder_wind_angle, glm::radians(90.0f), 0.001f));

    state.weather.wind = glm::vec3(0.0f);
    CHECK(!make_render_data(state, shipped_text_assets(), shipped_content().skills, render_meshes{}, test_props(), 70.0f, false)
               .rangefinder_wind_angle);
}

TEST_CASE("locking onto the flag flashes its ring, then holds it lit") {
    game_state state = dry_hole();
    const rangefinder_tuning& tuning = state.tuning.rangefinder;
    REQUIRE(tuning.jolt_flashes > 0);
    const float flash = tuning.jolt_seconds / static_cast<float>(tuning.jolt_flashes);
    CHECK(!rangefinder_lock_lit(state));
    state.rangefinder.flag = glm::vec3(0.0f);
    state.rangefinder_locked_seconds = flash * 0.25f;
    CHECK(rangefinder_lock_lit(state));
    state.rangefinder_locked_seconds = flash * 0.75f;
    CHECK(!rangefinder_lock_lit(state));
    state.rangefinder_locked_seconds = tuning.jolt_seconds + 1.0f;
    CHECK(rangefinder_lock_lit(state));
}

TEST_CASE("the rangefinder reads the tree it points at, and cannot lock onto a flag behind it") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area area = flat_area(tuning);
    const glm::vec3 player(0.0f, 0.0f, 100.0f);
    const glm::vec3 eye = player + world_up * 1.65f;
    // A tree: its trunk 3 up, its leaves 4 more.
    play_area wooded = area;
    solid stem = test_post(glm::vec3(0.0f, 0.0f, 130.0f), 0.5f, 3.0f);
    stem.kind = solid_kind::trunk;
    set_solids(wooded, {stem, test_solid(solid_shape::cone, solid_kind::leaves, glm::vec3(0.0f, 3.0f, 130.0f), 2.0f, 4.0f)});
    const play_area* trees = &wooded;
    const glm::vec3 flag(0.0f, 0.0f, 250.0f);

    const rangefinder_reading trunk =
        read_rangefinder(area, view_of({flag}, trees), player, eye, look_along_z(0.0f), test_rangefinder(), tuning.scale);
    CHECK(!trunk.flag);
    REQUIRE(trunk.distance_meters.has_value());
    CHECK(near(*trunk.distance_meters, 29.5f * tuning.scale.meters_per_world_unit, 0.01f));

    // Up into its leaves, which are nearer than the ground beyond.
    const float to_leaves = std::atan2(5.0f - eye.y, 30.0f);
    const rangefinder_reading leaves = read_rangefinder(area, view_of({}, trees), player, eye,
                                                        look_along_z(glm::degrees(to_leaves)), test_rangefinder(), tuning.scale);
    REQUIRE(leaves.distance_meters.has_value());
    CHECK(*leaves.distance_meters < 30.0f * tuning.scale.meters_per_world_unit);
    REQUIRE(leaves.height_meters.has_value());
    CHECK(*leaves.height_meters > 3.0f * tuning.scale.meters_per_world_unit);

    // With the tree out of the way the same look locks onto the flag.
    CHECK(read_rangefinder(area, view_of({flag}), player, eye, look_along_z(0.0f), test_rangefinder(), tuning.scale).flag);
}

TEST_CASE("the rangefinder locks onto any hole's flag, the one nearest its reticle") {
    const game_tuning& tuning = shipped_content().tuning;
    const play_area area = flat_area(tuning);
    const glm::vec3 player(0.0f, 0.0f, 100.0f);
    const glm::vec3 eye = player + world_up * 1.65f;
    const glm::vec3 near_flag(0.0f, 0.0f, 200.0f);
    const glm::vec3 beside_flag(4.0f, 0.0f, 200.0f);
    const float yaw = std::atan2(beside_flag.x - player.x, beside_flag.z - player.z);
    const glm::vec3 look = yaw_direction(yaw);
    REQUIRE(near(glm::dot(look, safe_normalize(horizontal(beside_flag - player), look)), 1.0f, 0.0001f));
    const rangefinder_reading reading =
        read_rangefinder(area, view_of({near_flag, beside_flag}), player, eye, look, test_rangefinder(), tuning.scale);
    REQUIRE(reading.flag.has_value());
    CHECK(near(*reading.flag, beside_flag));
}

TEST_CASE("the rangefinder meets carts and people standing in its way") {
    const glm::vec3 eye(0.0f, 1.5f, 0.0f);
    const glm::vec3 ahead(0.0f, 0.0f, 1.0f);
    CHECK(near(*post_hit(sight_post{glm::vec3(0.0f, 0.0f, 10.0f), 0.3f, 1.8f}, eye, ahead), 9.7f, 0.001f));
    CHECK(!post_hit(sight_post{glm::vec3(0.0f, 0.0f, 10.0f), 0.3f, 1.0f}, eye, ahead));
    CHECK(near(*box_hit(sight_box{glm::vec3(0.0f, 1.0f, 20.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.5f, 1.0f, 0.7f)}, eye, ahead),
               19.3f, 0.001f));
    CHECK(!box_hit(sight_box{glm::vec3(0.0f, 1.0f, -20.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.5f, 1.0f, 0.7f)}, eye, ahead));
    sight_scene scene;
    scene.posts.push_back(sight_post{glm::vec3(0.0f, 0.0f, 10.0f), 0.3f, 1.8f});
    scene.boxes.push_back(sight_box{glm::vec3(0.0f, 1.0f, 5.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(1.5f, 1.0f, 0.7f)});
    CHECK(near(*standing_hit(scene, eye, ahead, 100.0f), 4.3f, 0.001f));
    CHECK(!standing_hit(scene, eye, ahead, 4.0f));
}

TEST_CASE("the rangefinder reads true in the dry, wanders in light rain and mist, and reads nonsense in rain and fog") {
    const rangefinder_tuning& tuning = shipped_content().tuning.rangefinder;
    const weather_tuning& rules = shipped_content().tuning.weather;
    const auto in = [&](const weather_kind kind) { return with_weather_kind(weather_state{}, kind, rules); };
    rangefinder_reading clean;
    clean.distance_meters = 150.0f;
    clean.height_meters = 4.0f;
    clean.flag = glm::vec3(1.0f);
    for (const weather_kind dry : {weather_kind::clear, weather_kind::cloudy}) {
        CHECK(*weathered_reading(clean, in(dry), rules, tuning, 7).distance_meters == 150.0f);
    }
    const auto spread = [&](const weather_kind kind) {
        float low = 1.0e9f;
        float high = 0.0f;
        for (std::uint32_t ping = 0; ping < 200; ++ping) {
            const rangefinder_reading reading = weathered_reading(clean, in(kind), rules, tuning, ping);
            CHECK(reading.flag.has_value());
            CHECK(reading.distance_meters.has_value());
            if (!reading.distance_meters) {
                continue;
            }
            low = std::min(low, *reading.distance_meters);
            high = std::max(high, *reading.distance_meters);
        }
        return std::make_pair(low, high);
    };
    const auto [rain_low, rain_high] = spread(weather_kind::light_rain);
    CHECK(rain_low >= 150.0f * (1.0f - tuning.light_rain_error) - 0.01f);
    CHECK(rain_high <= 150.0f * (1.0f + tuning.light_rain_error) + 0.01f);
    CHECK(rain_high - rain_low > 150.0f * tuning.light_rain_error);
    const auto [mist_low, mist_high] = spread(weather_kind::light_fog);
    CHECK(mist_low >= 150.0f * (1.0f - tuning.light_fog_error) - 0.01f);
    CHECK(mist_high <= 150.0f * (1.0f + tuning.light_fog_error) + 0.01f);
    CHECK(mist_high - mist_low > 150.0f * tuning.light_rain_error * 2.0f);

    for (const weather_kind blind : {weather_kind::rain, weather_kind::heavy_rain, weather_kind::fog}) {
        int blanks = 0;
        int far_off = 0;
        for (std::uint32_t ping = 0; ping < 200; ++ping) {
            const rangefinder_reading reading = weathered_reading(clean, in(blind), rules, tuning, ping);
            CHECK(!reading.flag);
            if (!reading.distance_meters) {
                ++blanks;
            } else if (std::abs(*reading.distance_meters - 150.0f) > 150.0f * tuning.light_fog_error) {
                ++far_off;
            }
        }
        CHECK(blanks > 0);
        CHECK(far_off > 100);
    }
}

TEST_CASE("held up the rangefinder takes a new reading every ping") {
    game_state state = started_hole();
    state.weather_override = with_weather_kind(state.weather, weather_kind::heavy_rain, state.tuning.weather);
    raise_rangefinder(state);
    REQUIRE(state.rangefinder_active);
    const std::uint32_t first = state.rangefinder_ping;
    game_input input;
    input.rangefinder_held = true;
    const float frame = 0.02f;
    const int frames = static_cast<int>(std::ceil(3.0f * state.tuning.rangefinder.ping_seconds / frame));
    for (int i = 0; i < frames; ++i) {
        update_game(state, input, frame);
    }
    CHECK(state.rangefinder_ping >= first + 2);
    CHECK(state.rangefinder_ping <= first + 4);
}
