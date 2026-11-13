#include "doctest.h"

#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "game/green_read.h"
#include "game/pacing.h"
#include "game/text_assets.h"
#include "physics/material_zone.h"
#include "physics/vector_math.h"
#include "renderer/hud_overlay.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
render_data frame_for(const game_state& state) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), 60.0f, true);
}

glm::mat4 view_proj_of(const render_data& data, const float aspect) {
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    return glm::perspective(glm::radians(data.camera_fov_degrees), aspect, 0.05f, 2000.0f) * view;
}

// A bare hole from the origin up `rise` to its pin 100 m along +Z, its green
// from 40 to 70 m and 10 m either side; me standing at 45 m facing up it.
game_state on_green(const float rise) {
    game_state state = started_hole();
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, rise, 100.0f), 40.0f);
    hole.material_zones = {make_material_zone(material_zone_type::green, {{-10.0f, 40.0f}, {10.0f, 40.0f}, {10.0f, 70.0f}, {-10.0f, 70.0f}})};
    play_hole(state, hole);
    state.player.position = anchor_on_terrain(state.area, glm::vec3(0.0f, 0.0f, 45.0f));
    state.player.vertical = vertical_body{state.player.position.y, 0.0f, true};
    state.player.yaw = 0.0f;
    state.look.pitch = -0.3f;
    return state;
}

game_input forward() {
    game_input input;
    input.forward_held = true;
    return input;
}

game_input pacing_toggle() {
    game_input input;
    input.toggle_pacing = true;
    return input;
}

game_input crouching() {
    game_input input;
    input.crouch_held = true;
    return input;
}
}

TEST_CASE("Caps Lock starts or stops pacing, K holds up the skills and C crouches") {
    input_state caps;
    caps.caps_lock.is_down = true;
    caps.caps_lock.pressed = true;
    CHECK(game_input_from_keys(caps, 0.0f, setup_tuning{}).toggle_pacing);
    CHECK(!game_input_from_keys(caps, 0.0f, setup_tuning{}).skills_panel_held);
    caps.caps_lock.pressed = false;
    CHECK(!game_input_from_keys(caps, 0.0f, setup_tuning{}).toggle_pacing);

    input_state k;
    k.key_k.is_down = true;
    CHECK(game_input_from_keys(k, 0.0f, setup_tuning{}).skills_panel_held);

    input_state c;
    c.key_c.is_down = true;
    CHECK(game_input_from_keys(c, 0.0f, setup_tuning{}).crouch_held);
}

TEST_CASE("whole paces of pace_meters each") {
    CHECK(paces_in(0.0f, 1.0f) == 0);
    CHECK(paces_in(0.99f, 1.0f) == 0);
    CHECK(paces_in(7.2f, 1.0f) == 7);
    CHECK(paces_in(7.2f, 0.9f) == 8);
    CHECK(paces_in(5.0f, 0.0f) == 0);
}

TEST_CASE("pacing counts a pace for every pace_meters walked and pops the count up ahead of me") {
    game_state state = on_green(0.0f);
    state.tuning.pacing.pace_meters = 1.0f;
    update_game(state, pacing_toggle(), 0.05f);
    REQUIRE(state.pacing.has_value());
    REQUIRE(state.pace_count.has_value());
    CHECK(state.pace_count->paces == 0);

    const glm::vec3 start = state.player.position;
    while (horizontal_distance(state.player.position, start) < 10.1f) {
        update_game(state, forward(), 0.05f);
    }
    REQUIRE(state.pace_count.has_value());
    CHECK(state.pace_count->paces == 10);

    const render_data data = frame_for(state);
    REQUIRE(data.pace_number.has_value());
    CHECK(data.pace_number->label == "10");
    for (const float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
        const std::optional<glm::vec2> shown = project_to_screen(view_proj_of(data, aspect), data.pace_number->position);
        REQUIRE(shown.has_value());
        CHECK(std::abs(shown->x) < 0.1f);
        CHECK(std::abs(shown->y) < 0.9f);
    }

    // Standing, it holds; Caps Lock again stops it, and the count fades.
    for (int frame = 0; frame < 100; ++frame) {
        update_game(state, game_input{}, 0.05f);
    }
    REQUIRE(state.pace_count.has_value());
    update_game(state, pacing_toggle(), 0.05f);
    CHECK(!state.pacing.has_value());
    REQUIRE(state.pace_count.has_value());
    CHECK(state.pace_count->paces == 10);
    for (int frame = 0; frame < 100; ++frame) {
        update_game(state, forward(), 0.05f);
    }
    CHECK(!state.pace_count.has_value());
}

TEST_CASE("running, a hop and Esc each stop pacing") {
    game_input run = forward();
    run.run_held = true;
    game_input hop;
    hop.jump = true;
    game_input escape;
    escape.cancel = true;
    for (const game_input& breaks : {run, hop, escape}) {
        game_state state = on_green(0.0f);
        update_game(state, pacing_toggle(), 0.05f);
        update_game(state, forward(), 0.05f);
        REQUIRE(state.pacing.has_value());
        update_game(state, breaks, 0.05f);
        CHECK(!state.pacing.has_value());
    }

    // Shift held standing still breaks nothing.
    game_state state = on_green(0.0f);
    update_game(state, pacing_toggle(), 0.05f);
    game_input shift;
    shift.run_held = true;
    update_game(state, shift, 0.05f);
    CHECK(state.pacing.has_value());
}

TEST_CASE("crouched I move slowly, my eyes low over the green") {
    game_state state = on_green(3.0f);
    const glm::vec3 standing = state.player.position;
    for (int frame = 0; frame < 20; ++frame) {
        game_input input = crouching();
        input.forward_held = true;
        input.jump = true;
        input.run_held = true;
        update_game(state, input, 0.05f);
    }
    CHECK(near(horizontal_distance(state.player.position, standing),
               state.tuning.player.walk_speed * state.tuning.green_read.walk_factor, 0.001f));
    CHECK(state.player.vertical.grounded);
    CHECK(near(state.crouch, 1.0f));
    const render_data data = frame_for(state);
    CHECK(near(data.camera_position.y - state.player.position.y, state.tuning.green_read.eye_height, 0.001f));
    // My figure crouches with me, as others see me.
    REQUIRE(data.my_figure.has_value());
    CHECK(near(data.my_figure->crouch, 1.0f));

    for (int frame = 0; frame < 20; ++frame) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(near(state.crouch, 0.0f));
    CHECK(frame_for(state).green_read_dots.empty());
}

TEST_CASE("crouched on a green its dots lie on the green only, none by the cup") {
    game_state state = on_green(3.0f);
    state.player.position = anchor_on_terrain(state.area, glm::vec3(0.0f, 0.0f, 66.0f));
    for (int frame = 0; frame < 20; ++frame) {
        update_game(state, crouching(), 0.05f);
    }
    const render_data data = frame_for(state);
    REQUIRE(!data.green_read_dots.empty());
    for (const render_read_dot& dot : data.green_read_dots) {
        CHECK(dot.position.z > 40.0f - state.tuning.green_read.drift_meters);
        CHECK(dot.position.z < 70.0f);
        CHECK(horizontal_distance(dot.position, data.pin_position) >= state.tuning.green_read.clear_meters - 0.001f);
        // A dot at the edge may slide on past it, faded out.
        const float reach = state.tuning.green_read.radius_meters + state.tuning.green_read.drift_meters;
        CHECK(horizontal_distance(dot.position, state.player.position) <= reach + 0.001f);
    }
}

TEST_CASE("looking up a green that rises away from me, its dots slide down the screen towards me") {
    game_state state = on_green(3.0f);
    for (int frame = 0; frame < 20; ++frame) {
        update_game(state, crouching(), 0.05f);
    }
    const render_data before = frame_for(state);
    update_game(state, crouching(), 0.05f);
    const render_data after = frame_for(state);
    REQUIRE(before.green_read_dots.size() == after.green_read_dots.size());
    REQUIRE(!before.green_read_dots.empty());

    const glm::mat4 view_proj = view_proj_of(before, 16.0f / 9.0f);
    int slid = 0;
    for (std::size_t i = 0; i < before.green_read_dots.size(); ++i) {
        const glm::vec3 a = before.green_read_dots[i].position;
        const glm::vec3 b = after.green_read_dots[i].position;
        // A dot starting its slide again jumps back up the slope.
        if (b.z > a.z) {
            continue;
        }
        CHECK(b.z < a.z);
        CHECK(b.y < a.y);
        // Down the hole's slope, the ribbon's camber aside.
        CHECK(std::abs(b.x - a.x) < a.z - b.z);
        const std::optional<glm::vec2> from = project_to_screen(view_proj, a);
        const std::optional<glm::vec2> to = project_to_screen(view_proj, b);
        if (from && to && std::abs(from->x) < 1.0f && std::abs(from->y) < 1.0f) {
            CHECK(to->y < from->y);
            ++slid;
        }
    }
    CHECK(slid > 5);
}

TEST_CASE("on a level green the dots stay put") {
    game_state state = on_green(0.0f);
    for (int frame = 0; frame < 20; ++frame) {
        update_game(state, crouching(), 0.05f);
    }
    const render_data before = frame_for(state);
    update_game(state, crouching(), 0.05f);
    const render_data after = frame_for(state);
    REQUIRE(!before.green_read_dots.empty());
    REQUIRE(before.green_read_dots.size() == after.green_read_dots.size());
    for (std::size_t i = 0; i < before.green_read_dots.size(); ++i) {
        CHECK(near(before.green_read_dots[i].position, after.green_read_dots[i].position, 0.0001f));
    }
}


TEST_CASE("pacing and crouching change the very next step and restore normal speed immediately") {
    game_state state = on_green(0.0f);
    const float walk = state.tuning.player.walk_speed;
    game_input paced = forward();
    paced.toggle_pacing = true;
    const glm::vec3 start = state.player.position;
    update_game(state, paced, move_step_seconds);
    REQUIRE(state.pacing);
    CHECK(near(state.player.speed, walk * state.tuning.pacing.walk_factor));
    CHECK(near(state.pacing->meters, horizontal_distance(start, state.player.position)));
    update_game(state, paced, move_step_seconds);
    CHECK(!state.pacing);
    CHECK(near(state.player.speed, walk));

    game_input read = forward();
    read.crouch_held = true;
    read.run_held = true;
    read.jump = true;
    update_game(state, read, move_step_seconds);
    CHECK(near(state.player.speed, walk * state.tuning.green_read.walk_factor));
    CHECK(state.player.vertical.grounded);
    CHECK(!state.player.hop_pending);
    update_game(state, forward(), move_step_seconds);
    CHECK(near(state.player.speed, walk));
    CHECK(state.player.vertical.grounded);

    for (const int cancel : {0, 1, 2}) {
        game_state stopped = on_green(0.0f);
        update_game(stopped, paced, move_step_seconds);
        game_input breaks = forward();
        breaks.run_held = cancel == 0;
        breaks.jump = cancel == 1;
        breaks.cancel = cancel == 2;
        update_game(stopped, breaks, move_step_seconds);
        CHECK(!stopped.pacing);
        CHECK(near(stopped.player.speed, walk * (breaks.run_held ? stopped.tuning.player.run_factor : 1.0f)));
        CHECK(stopped.player.vertical.grounded == !breaks.jump);
        // A toggle cannot restart pacing in the same step as a cancellation.
        breaks.toggle_pacing = true;
        update_game(stopped, breaks, move_step_seconds);
        CHECK(!stopped.pacing);
    }
}

TEST_CASE("pacing and green reading preserve normalized directional movement and walking effects") {
    for (const bool paced : {false, true}) {
        for (const bool crouched : {false, true}) {
            for (const bool boosted : {false, true}) {
                for (const int forward_sign : {-1, 0, 1}) {
                    for (const int side_sign : {-1, 0, 1}) {
                        if (forward_sign == 0 && side_sign == 0) { continue; }
                        game_state state = on_green(0.0f);
                        if (boosted) {
                            state.effects.push_back(active_effect{"test", item_effect{effect_target_walk_speed,
                                effect_bucket::increased, 0.5f, effect_duration::equipped}, 0, effect_never_ends});
                        }
                        game_input input;
                        input.forward_held = forward_sign == 1;
                        input.back_held = forward_sign == -1;
                        input.strafe_left_held = side_sign == 1;
                        input.strafe_right_held = side_sign == -1;
                        input.toggle_pacing = paced;
                        input.crouch_held = crouched;
                        const glm::vec3 from = state.player.position;
                        update_game(state, input, move_step_seconds);
                        float factor = paced ? state.tuning.pacing.walk_factor : 1.0f;
                        if (crouched) { factor = std::min(factor, state.tuning.green_read.walk_factor); }
                        const float distance = state.tuning.player.walk_speed * factor * (boosted ? 1.5f : 1.0f) * move_step_seconds;
                        CHECK(near(horizontal_distance(from, state.player.position), distance, 0.00001f));
                        const glm::vec3 travel = state.player.position - from;
                        const glm::vec3 direction = glm::normalize(glm::vec3(
                            static_cast<float>(side_sign), 0.0f, static_cast<float>(forward_sign)));
                        CHECK(near(travel, direction * distance, 0.00001f));
                    }
                }
            }
        }
    }
}


TEST_CASE("pacing counts only the distance collision permits") {
    game_state state = on_green(0.0f);
    const glm::vec3 from = state.player.position;
    solid wall;
    wall.base = from + glm::vec3(0.0f, 0.0f, 1.0f);
    wall.radius = 0.5f;
    wall.height = 4.0f;
    wall.stops_walker = true;
    state.area.solids.push_back(wall);
    state.area.solids_grid = build_solid_grid(state.area.solids, 8.0f);
    game_input input = forward();
    input.toggle_pacing = true;
    for (int frame = 0; frame < 40; ++frame) {
        update_game(state, input, 0.05f);
        input.toggle_pacing = false;
    }
    REQUIRE(state.pacing);
    CHECK(state.pacing->meters > 0.0f);
    CHECK(state.pacing->meters < 0.3f);
    CHECK(near(state.pacing->meters, horizontal_distance(from, state.player.position), 0.0001f));
    CHECK(state.pace_count->paces == 0);
}

TEST_CASE("pacing stops at the first airborne step within a frame") {
    game_state state = on_green(0.0f);
    // Just past a ledge, the feet still have its height until the next step.
    state.player.position.y += state.tuning.player.step_up + 1.0f;
    state.player.vertical = vertical_body{state.player.position.y, 0.0f, true};
    const glm::vec3 from = state.player.position;
    game_input input = forward();
    input.toggle_pacing = true;
    update_game(state, input, 3.0f * move_step_seconds);
    CHECK(!state.player.vertical.grounded);
    CHECK(!state.pacing);
    REQUIRE(state.pace_count.has_value());
    CHECK(state.pace_count->paces == 0);
    CHECK(near(state.player.speed, state.tuning.player.walk_speed));
    const float distance = state.tuning.player.walk_speed * move_step_seconds *
        (state.tuning.pacing.walk_factor + 2.0f);
    CHECK(near(horizontal_distance(from, state.player.position), distance, 0.00001f));
}
