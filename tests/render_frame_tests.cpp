#include "doctest.h"
#include "renderer/online_overlay.h"

#include "core/render_frame.h"
#include "core/tape_tracking.h"
#include "game/cart_seats.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"
#include "renderer/bag_batch.h"
#include "renderer/cart_batch.h"
#include "renderer/held_props.h"
#include "renderer/prop_batch.h"
#include "renderer/prop_frame.h"
#include "renderer/primitive_mesh.h"
#include "renderer/remote_avatar_batch.h"
#include "renderer/fence_batch.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/marker_scene.h"
#include "renderer/shadow_casters.h"
#include "renderer/water_reflection.h"

#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr float test_fov_degrees = 60.0f;
constexpr int shadow_picture_height = 240;

// What the sun's shadow map draws of the frame's markers: figures and flagsticks.
struct sun_casters {
    std::vector<world_marker_vertex> list;
    const std::vector<world_marker_vertex>& vertices() const { return list; }
};

void build_shadow_casters(sun_casters& casters, const render_data& data, const scene_colors& colors) {
    world_marker_batch batch;
    build_world_marker_batch(batch, marker_scene_for(data, shadow_picture_height), colors);
    casters.list.clear();
    for (const world_marker_run& run : batch.runs()) {
        if (batch.shown_in(run, marker_view::sun_shadow)) {
            casters.list.insert(casters.list.end(), batch.vertices().begin() + static_cast<std::ptrdiff_t>(run.first),
                                batch.vertices().begin() + static_cast<std::ptrdiff_t>(run.first + run.count));
        }
    }
    // The figures, each canonical mesh placed by its instance.
    const canonical_meshes meshes;
    for (const world_marker_instance_run& run : batch.instance_runs()) {
        if (!batch.shown_in(run.part, marker_view::sun_shadow)) { continue; }
        for (std::size_t n = run.first; n < run.first + run.count; ++n) {
            const mesh_instance& instance = batch.instances(run.mesh)[n];
            for (std::size_t v = 0; v < meshes.vertex_count(run.mesh); ++v) {
                casters.list.push_back(world_marker_vertex{placed_position(instance, meshes, run.mesh, v), instance.color, instance.glow});
            }
        }
    }
}

render_data frame_for(const game_state& state, const float fov_degrees = test_fov_degrees) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), fov_degrees, true);
}
}

TEST_CASE("a hole frame shows the ball, every tee box and the pin") {
    game_state state = started_hole();
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);

    CHECK(data.show_ball);
    CHECK(data.show_hole);
    CHECK(data.pin_position == state.static_anchors.pins[0]);
    CHECK(data.tee_position == state.static_anchors.tees[0]);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == state.area.tee_boxes.size());
    CHECK(data.camera_fov_degrees == test_fov_degrees);
}

TEST_CASE("the interact prompt shows beside the ball on foot, not away from it, over it or with the setting off") {
    game_state state = started_hole();
    REQUIRE(interact_available(state));
    CHECK(frame_for(state).show_interact_prompt);
    CHECK(!make_render_data(state, *load_text_assets(asset_root()), shipped_content().skills, render_meshes{}, test_props(), test_fov_degrees, false)
               .show_interact_prompt);

    state.mode = game_mode::addressing;
    CHECK(!frame_for(state).show_interact_prompt);

    state.mode = game_mode::walking;
    state.player.position = state.ball->position + glm::vec3(60.0f, 0.0f, 0.0f);
    CHECK(!interact_available(state));
    CHECK(!frame_for(state).show_interact_prompt);
}

TEST_CASE("the drawn ball sits on the ground, not sunk into it") {
    game_state state = started_hole();
    const render_data data = frame_for(state);
    const float bottom = data.ball_position.y - data.ball_visual_radius_meters;
    CHECK(near(bottom, terrain_height(state.area, state.ball->position), 0.001f));
    CHECK(near(horizontal(data.ball_position), horizontal(state.ball->position)));
}

TEST_CASE("with my ball in my pocket a frame hides it and shows every tee box, pin and claimable collectible") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(!data.show_ball);
    CHECK(!data.show_hole);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == 3U);
    CHECK(data.pin_markers.size() == 3U);
    // The fitness cache needs fitness level 2, so two of three are claimable.
    CHECK(data.collectibles.size() == 2U);
}

TEST_CASE("with my ball in play every other hole's pin stays up, and the collectibles too") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 1, false));
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(data.show_hole);
    CHECK(data.collectibles.size() == 2U);
    CHECK(data.pin_position == state.static_anchors.pins[1]);
    const std::vector<glm::vec3>& all = state.static_anchors.pins;
    REQUIRE(all.size() == 3U);
    REQUIRE(data.pin_markers.size() == 2U);
    CHECK(data.pin_markers[0] == all[0]);
    CHECK(data.pin_markers[1] == all[2]);
    CHECK(near(all[1], data.pin_position));
}

TEST_CASE("a ball left in its cup is drawn there until it is picked up") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    state.ball->strokes = 3;
    complete_current_hole(state);
    REQUIRE(state.ball.has_value());
    REQUIRE(state.ball->place == ball_place::in_cup);
    state.player.position = state.ball->position;
    refresh_static_anchor_cache(state);
    render_data data = frame_for(state);
    CHECK(data.show_ball);
    CHECK(!data.show_hole);
    CHECK(data.pin_markers.size() == 3U);  // its own pin among them
    // Down in its cup, on the bottom, its top just under the rim.
    const cup& hole = state.area.cups[0];
    CHECK(near(horizontal(data.ball_position), horizontal(hole.centre), 0.001f));
    CHECK(near(data.ball_position.y - data.ball_visual_radius_meters, hole.centre.y - hole.depth, 0.0001f));
    CHECK(data.ball_position.y + data.ball_visual_radius_meters < hole.centre.y);

    pick_up_cup_ball(state);
    data = frame_for(state);
    CHECK(!data.show_ball);
}

TEST_CASE("the give-up question names the hole my ball is in play on, only while it is asked") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(tee_up(state, 0, false));
    CHECK(frame_for(state).give_up_label.empty());
    state.player.position = state.world.hole_starts[2].position;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt.has_value());
    const render_data data = frame_for(state);
    CHECK(data.give_up_label.find('1') != std::string::npos);
    CHECK(!data.show_interact_prompt);
}

TEST_CASE("every camera rig uses the field of view setting") {
    game_state state = started_hole();
    CHECK(frame_for(state, 75.0f).camera_fov_degrees == 75.0f);
    address_looking_down(state);
    REQUIRE(active_camera_rig(state) == camera_rig::addressing);
    CHECK(live_camera_view(state, 50.0f).fov_degrees == 50.0f);
    start_swing_on_time(state);
    swing_through(state);
    REQUIRE(active_camera_rig(state) == camera_rig::following_shot);
    CHECK(live_camera_view(state, 90.0f).fov_degrees == 90.0f);
}

TEST_CASE("each game mode has its camera rig") {
    game_state state = started_hole();
    CHECK(active_camera_rig(state) == camera_rig::walking);
    enter_addressing(state);
    CHECK(active_camera_rig(state) == camera_rig::addressing);
    // At my eyes over the stance, beside the ball.
    const camera_view eyes = live_camera_view(state, test_fov_degrees);
    CHECK(eyes.position.y > state.ball->position.y + 1.0f);
    CHECK(horizontal_distance(eyes.position, state.ball->position) < 1.0f);

    state.address.look = down_look(state.tuning.camera);
    start_swing_on_time(state);
    swing_through(state);
    CHECK(active_camera_rig(state) == camera_rig::following_shot);

    // The follow camera stays at the address view of the shot's start.
    const camera_view follow = live_camera_view(state, test_fov_degrees);
    update_game(state, game_input{}, 0.05f);
    CHECK(near(live_camera_view(state, test_fov_degrees).position, follow.position));
}

TEST_CASE("the preview is only the start of the simulated shot in still air: from the ball, start_line_meters out") {
    game_state state = started_hole();
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    const render_data data = frame_for(state);
    REQUIRE(data.osd_line.size() > 2U);
    const glm::vec3 ball = state.address.setup.ball_start;
    CHECK(near(data.osd_line.front(), ball, 0.01f));

    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, shot_unix_ms(state));
    course.weather.wind = glm::vec3(0.0f);
    const shot_result shot = simulate_shot(state.address.setup, course, state.tuning, state.clubs);
    const float step = state.tuning.shot_preview.step_seconds;
    for (std::size_t i = 0; i < data.osd_line.size(); ++i) {
        CHECK(near(data.osd_line[i], shot_position_at(shot, static_cast<float>(i) * step), 0.001f));
    }
    // It ends just past start_line_meters, nowhere near where the shot lands.
    const float start_line = state.tuning.shot_preview.start_line_meters / state.tuning.scale.meters_per_world_unit;
    CHECK(horizontal_distance(data.osd_line.back(), ball) >= start_line);
    CHECK(horizontal_distance(data.osd_line[data.osd_line.size() - 2], ball) < start_line);
    CHECK(horizontal_distance(shot.rest_position, ball) > 5.0f * start_line);

    // Only a changed setup is simulated again.
    const std::vector<glm::vec3> before = state.address.preview->line;
    update_game(state, game_input{}, 0.016f);
    CHECK(state.address.preview->line == before);
    game_input longer;
    longer.backswing_steps = -10;
    update_game(state, longer, 0.016f);
    CHECK(state.address.preview->line != before);
}

TEST_CASE("a chip's preview runs to where it first comes down and ends there, fading out over its flight") {
    game_state state = started_hole();
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    shot_setup chip = state.address.setup;
    chip.club_id = "lob_wedge";
    chip.backswing = 0.08f;
    const shot_preview preview = preview_shot(state, chip);
    REQUIRE(preview.line.size() > 2U);

    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, shot_unix_ms(state));
    course.weather.wind = glm::vec3(0.0f);
    const shot_result shot = simulate_shot(chip, course, state.tuning, state.clubs);
    const glm::vec3 ball = chip.ball_start;
    const auto landing = std::find_if(shot.events.begin(), shot.events.end(),
                                      [](const shot_event& event) { return event.kind == shot_event_kind::land; });
    REQUIRE(landing != shot.events.end());
    const glm::vec3 lands = shot_position_at(shot, landing->time);
    const float start_line = state.tuning.shot_preview.start_line_meters / state.tuning.scale.meters_per_world_unit;
    REQUIRE(horizontal_distance(lands, ball) < start_line);
    // It comes down where the shot does, and shows nothing of the roll after.
    CHECK(near(preview.line.back(), lands, 0.001f));
    CHECK(preview.line[preview.line.size() / 2].y > ball.y);
    CHECK(horizontal_distance(preview.line.back(), ball) < horizontal_distance(shot.rest_position, ball));
}

TEST_CASE("the cart I drive is drawn whole where it stands, the camera at my seat's eyes whichever way I look") {
    game_state state = started_game(fixture_hub_course());
    const cart_tuning& tuning = state.tuning.cart;
    state.ride = cart_ride{};
    state.player.position = anchor_on_terrain(state.area, seat_position(state.cart.pose, cart_seat::driver, tuning));
    state.player.yaw = state.cart.pose.yaw;
    for (const float head : {0.0f, 1.6f, 3.1f}) {
        state.look.head_yaw = head;
        const render_data data = frame_for(state);
        REQUIRE(data.carts.size() == 1U);
        // Set on its wheels, a little over the ground under its seat.
        CHECK(near(horizontal(data.carts[0].position), horizontal(state.cart.pose.position)));
        CHECK(near(data.carts[0].position.y, state.cart.pose.position.y, 0.1f));
        CHECK(near(data.carts[0].yaw, state.cart.pose.yaw, 0.0001f));
        CHECK(data.carts[0].driven);
        CHECK(near(data.camera_position, state.player.position + world_up * state.tuning.camera.walking_eye_height));
        CHECK(near(horizontal(data.camera_position),
                   horizontal(seat_position(cart_pose{data.carts[0].position, data.carts[0].yaw}, cart_seat::driver, tuning)),
                   0.001f));
        const glm::vec3 look = horizontal(data.camera_target - data.camera_position);
        CHECK(glm::dot(glm::normalize(look), yaw_direction(state.cart.pose.yaw + head)) > 0.999f);
    }
}

TEST_CASE("my figure casts the sun's shadow where I stand, at my ball while I address it, and none from a cart's seat") {
    game_state state = started_hole();
    render_data data = frame_for(state);
    REQUIRE(data.my_figure.has_value());
    CHECK(near(data.my_figure->position, state.player.position));
    CHECK(data.my_figure->yaw == state.player.yaw);
    CHECK(!data.my_figure->swing.has_value());
    CHECK(data.avatar_eye_height == state.tuning.camera.walking_eye_height);
    sun_casters casters;
    build_shadow_casters(casters, data, test_scene_colors());
    // Mine, and the flagsticks.
    render_data flags_only = data;
    flags_only.my_figure.reset();
    sun_casters flag_casters;
    build_shadow_casters(flag_casters, flags_only, test_scene_colors());
    CHECK(!flag_casters.vertices().empty());
    CHECK(casters.vertices().size() == remote_figure_vertex_count() + flag_casters.vertices().size());
    REQUIRE(!data.carts.empty());
    CHECK(data.carts[0].model == "golf_cart");
    // My cart with my bag strapped on it, and the club in my hand.
    REQUIRE(data.carts[0].bags.size() == 1U);
    CHECK(data.carts[0].bags.begin()->second.model == "golf_bag");
    prop_frame frame;
    frame.build(data, test_props(), test_scene_colors(), 0.0f);
    const prop_ranges casting = frame.ranges(prop_view::sun_shadow, false);
    std::size_t casting_vertices = 0;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const prop_runs& runs = casting[static_cast<std::size_t>(mesh)];
        for (int i = 0; i < runs.count; ++i) {
            casting_vertices += runs.run[static_cast<std::size_t>(i)].count * prop_mesh_vertex_count(mesh);
        }
    }
    CHECK(casting_vertices == data.carts.size() * cart_vertex_count(test_props().at("golf_cart")) +
                                  prop_model_vertex_count(test_props().at("golf_bag")) +
                                  prop_model_vertex_count(test_props().at(data.my_figure->carried_club_model)));

    step_up_to_ball(state);
    data = frame_for(state);
    REQUIRE(data.my_figure.has_value());
    REQUIRE(data.my_figure->swing.has_value());
    CHECK(near(data.my_figure->position, address_stance_position(state.area, state.address.setup, state.tuning)));
    CHECK(near(data.my_figure->yaw, address_facing_yaw(state.address.setup), 0.0001f));

    state.mode = game_mode::walking;
    state.ride = cart_ride{};
    CHECK(!frame_for(state).my_figure.has_value());
}

TEST_CASE("the flagstick casts its own fixed geometry into the sun shadow map") {
    game_state state = started_hole();
    render_data data = frame_for(state);
    REQUIRE(data.show_hole);
    data.my_figure.reset();
    data.pin_markers.clear();
    data.lying_flags.clear();
    data.flags_out.clear();
    data.lighting.sun_direction = glm::normalize(glm::vec3(1.0f, 1.0f, 0.0f));
    sun_casters casters;
    build_shadow_casters(casters, data, test_scene_colors());
    REQUIRE(!casters.vertices().empty());
    world_marker_batch visible;
    const flag_pose flag{data.weather.flag_wind,data.weather.flag_lift,data.weather.flag_phase,data.weather.flag_flap};
    // The shadow map draws the pole as wide as the main view does.
    const world_marker_scene scene=marker_scene_for(data,shadow_picture_height);
    const float pole_radius=flagstick_draw_radius(scene,data.pin_position,data.pin_visual_height_meters,scene.pixel_per_meter);
    append_pin_flagstick(visible,data.pin_position,data.pin_visual_height_meters,test_scene_colors().markers,
                         test_scene_colors().glow,flag,pole_radius);
    REQUIRE(casters.vertices().size()==visible.vertices().size());
    for(std::size_t i=0;i<visible.vertices().size();++i) {
        CHECK(near(casters.vertices()[i].position,visible.vertices()[i].position));
    }
    const std::size_t pole_vertices=primitive_cylinder_segments*12;
    for(std::size_t i=0;i<pole_vertices;++i) {
        const auto offset=casters.vertices()[i].position-data.pin_position;
        CHECK(glm::length(horizontal(offset))<=pole_radius+0.0001f);
        CHECK(offset.y>=-0.0001f);
        CHECK(offset.y<=data.pin_visual_height_meters+0.0001f);
    }

    // Taken out, it lies there and still casts; standing, nothing does.
    data.flags_out.push_back(data.pin_position);
    build_shadow_casters(casters, data, test_scene_colors());
    CHECK(casters.vertices().empty());
    data.lying_flags.emplace_back(data.pin_position + glm::vec3(0.5f, 0.0f, 0.0f), data.pin_position + glm::vec3(2.5f, 0.0f, 0.0f));
    build_shadow_casters(casters, data, test_scene_colors());
    CHECK(!casters.vertices().empty());
}

TEST_CASE("online frames show other players, their tags, balls and trails; offline ones none") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    room_player other;
    other.name = "OTHER";
    other.group_id = 4;
    state.online.players[2] = other;
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(3.0f, 0.0f, 0.0f);
    avatar.mode = motion_mode::cart;
    state.remote_avatars[2] = avatar;
    state.online.carts[1] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    state.online.carts[2] = room_cart{glm::vec3(50.0f, 0.0f, 50.0f), 0.0f, 0, cart_condition::upright, 2};
    state.online.balls[2] = room_ball{0, glm::vec3(1.0f), 1};
    remote_shot shot;
    shot.result.duration = 1.0f;
    shot.result.trajectory = {glm::vec3(0.0f), glm::vec3(1.0f)};
    shot.elapsed = 2.0f;
    shot.trail.points = {glm::vec3(0.0f), glm::vec3(1.0f)};
    shot.trail.rested_seconds = 1.0f;
    state.remote_shots[2] = shot;

    render_data data = frame_for(state);
    CHECK(data.remote_avatars.empty());
    CHECK(data.carts.size() == 1U);  // offline, only mine
    CHECK(data.name_tags.empty());

    state.play = play_mode::online;
    data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    CHECK(data.remote_avatars[0].seated);
    CHECK(data.remote_avatars[0].at_wheel);
    // Their cart round them, its headlights on, and mine parked.
    REQUIRE(data.carts.size() == 2U);
    CHECK(!data.carts[0].driven);
    CHECK(data.carts[1].driven);
    CHECK(near(horizontal(seat_position(cart_pose{data.carts[1].position, data.carts[1].yaw}, cart_seat::driver,
                                        state.tuning.cart)),
               horizontal(avatar.position), 0.001f));
    CHECK(data.remote_avatars[0].group_id == 4U);
    REQUIRE(data.name_tags.size() == 1U);
    CHECK(data.name_tags[0].name == "OTHER");
    CHECK(data.name_tags[0].position.y > avatar.position.y);
    CHECK(data.name_tags[0].relationship == player_relationship::unknown);
    CHECK(data.remote_balls.size() == 1U);
    REQUIRE(data.flight_lines.size() == 1U);
    CHECK(data.flight_lines[0].alpha < state.tuning.flight_path.alpha);
    CHECK(data.avatar_eye_height == state.tuning.camera.walking_eye_height);

    // In my group, their tag says so; another group's doesn't.
    room_player me;
    me.group_id = 4;
    state.online.players[1] = me;
    data = frame_for(state);
    REQUIRE(data.name_tags.size() == 1U);
    CHECK(data.name_tags[0].relationship == player_relationship::grouped);
    state.online.players[1].group_id = 5;
    CHECK(frame_for(state).name_tags[0].relationship == player_relationship::unknown);
    state.online.players[1].group_id = 0;
    state.online.players[2].group_id = 0;
    CHECK(frame_for(state).name_tags[0].relationship == player_relationship::unknown);
}

TEST_CASE("another player is drawn smoking, and at their ball addressing it") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.play = play_mode::online;
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    state.online.players[2] = room_player{};
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(6.0f, 0.0f, 0.0f);
    avatar.smoking = item_use_shown{state.default_loadout.smokable, 0.5f};
    state.remote_avatars[2] = avatar;

    render_data data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    REQUIRE(data.remote_avatars[0].smoking.has_value());
    CHECK(data.remote_avatars[0].smoking->elapsed == 0.5f);
    CHECK(data.remote_avatars[0].smoking->model == state.items.find(state.default_loadout.smokable)->model);
    CHECK(!data.remote_avatars[0].drinking.has_value());
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].crouch == 0.0f);
    // Crouched reading the green, as their motion has them.
    state.remote_avatars[2].crouch = 0.75f;
    CHECK(frame_for(state).remote_avatars[0].crouch == 0.75f);
    state.remote_avatars[2].crouch = 0.0f;

    // Standing by it: turned the way they face, where they stand.
    const glm::vec3 ball = state.player.position + glm::vec3(0.0f, 0.0f, 4.0f);
    const float aim = 0.5f;
    net_motion aiming;
    aiming.mode = motion_mode::idle;
    aiming.position = avatar.position;
    aiming.yaw = aim;
    state.online.players[2].motion = aiming;
    state.online.players[2].motion_at = 4;
    state.online.balls[2] = room_ball{0, ball, 0};
    state.remote_avatars[2].yaw = aim;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(near(data.remote_avatars[0].position, avatar.position));
    CHECK(data.remote_avatars[0].yaw == aim);

    // Addressing it: stood at it holding the club, the club down.
    shot_setup theirs;
    theirs.ball_start = ball;
    theirs.aim_angle = aim;
    net_motion motion;
    motion.mode = motion_mode::address;
    motion.position = avatar.position;
    motion.yaw = aim;
    motion.setup = sent_setup_of(theirs);
    state.online.players[2].motion = motion;
    state.online.bags[2] = room_bag{bag_place{}, state.clubs.front().id};
    state.online.players[2].motion_at = 5;
    data = frame_for(state);
    REQUIRE(data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].swing->raise == 0.0f);
    const glm::vec3 drawn_ball = drawn_ball_center(ball, state.tuning.scale);
    CHECK(near(data.remote_avatars[0].swing->ball_position, drawn_ball));
    CHECK(near(data.remote_avatars[0].swing->aim_angle, aim, 0.0001f));
    // Where I would stand to their setup, side on to it.
    const glm::vec3 stance = address_stance_position(state.area, theirs, state.tuning);
    CHECK(near(data.remote_avatars[0].position, stance, 0.001f));
    CHECK(near(data.remote_avatars[0].yaw, address_facing_yaw(theirs), 0.0001f));
    // An arm's length from the ball, facing it.
    CHECK(horizontal_distance(stance, ball) < 1.0f);
    CHECK(near(data.remote_avatars[0].yaw, yaw_towards(stance, ball), 0.3f));

    // Once the shot of that swing is hit, they look where they hit it.
    state.remote_avatars[2].hit_motion_at = 5;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].yaw == aim);
}

TEST_CASE("another player's club and bag are drawn from their rows, and nothing before them") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.play = play_mode::online;
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    state.online.players[2] = room_player{};
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(6.0f, 0.0f, 0.0f);
    state.remote_avatars[2] = avatar;
    render_data data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    CHECK(data.remote_avatars[0].carried_club_model.empty());
    CHECK(data.remote_avatars[0].bag_model.empty());

    // Their bag row: the club in their hand; the bag waits for their loadout row.
    const club_definition& club = state.clubs.back();
    state.online.bags[2] = room_bag{bag_place{}, club.id};
    data = frame_for(state);
    CHECK(data.remote_avatars[0].carried_club_model == club.model);
    CHECK(data.remote_avatars[0].bag_model.empty());

    // Their loadout row, its bag slot empty: the default bag, as their own game has it.
    state.online.loadouts[2] = loadout{};
    data = frame_for(state);
    CHECK(data.remote_avatars[0].bag_model == state.items.find(state.default_loadout.bag)->model);
}

TEST_CASE("another player at their ball is drawn from their row as their own game draws them, setup and all") {
    // Their game: the driver, the ball forward, the aim turned 10 degrees and the face open.
    game_state golfer = started_hole();
    refresh_static_anchor_cache(golfer);
    const auto driver = std::find_if(golfer.clubs.begin(), golfer.clubs.end(),
                                     [](const club_definition& club) { return club.club_class == "driver"; });
    REQUIRE(driver != golfer.clubs.end());
    golfer.selected_club = static_cast<std::size_t>(driver - golfer.clubs.begin());
    step_up_to_ball(golfer);
    REQUIRE(golfer.mode == game_mode::addressing);
    const shot_setup stock = golfer.address.setup;
    shot_setup turned = stock;
    turned.aim_angle = wrap_angle(stock.aim_angle + glm::radians(10.0f));
    turned.face_open = glm::radians(4.0f);
    turned.ball_position = 1.0f;
    golfer.address.setup = clamped_setup(turned, setup_limits_for(*driver, golfer.tuning.setup));
    REQUIRE(golfer.address.setup.ball_position != stock.ball_position);
    update_game(golfer, game_input{}, 0.0f);
    const render_data mine = frame_for(golfer);
    REQUIRE(mine.my_figure.has_value());
    REQUIRE(mine.my_figure->swing.has_value());

    // Through their avatar row as the server keeps it, with the bag row's club.
    net_motion motion = current_motion(golfer);
    REQUIRE(motion.mode == motion_mode::address);
    motion.setup = clamped_sent_setup(motion.setup, *driver, golfer.tuning.setup);
    game_state observer = started_hole();
    refresh_static_anchor_cache(observer);
    observer.play = play_mode::online;
    observer.online.account_id = 1;
    observer.online.status = net_status::connected;
    room_player player;
    player.hand = golfer.hand;
    player.motion = motion;
    player.motion_at = 5;
    observer.online.players[2] = player;
    observer.online.balls[2] = room_ball{0, golfer.ball->position, 0};
    observer.online.bags[2] = room_bag{bag_place{}, driver->id};
    remote_avatar avatar;
    avatar.position = motion.position;
    observer.remote_avatars[2] = avatar;
    const render_data theirs = frame_for(observer);
    REQUIRE(theirs.remote_avatars.size() == 1U);
    const render_remote_avatar& shown = theirs.remote_avatars[0];
    REQUIRE(shown.swing.has_value());

    CHECK(near(shown.position, mine.my_figure->position, 0.001f));
    CHECK(near(shown.yaw, mine.my_figure->yaw, 0.001f));
    CHECK(shown.left_handed == mine.my_figure->left_handed);
    const render_club_pose& club = *shown.swing;
    const render_club_pose& own = *mine.my_figure->swing;
    CHECK(club.club_model == own.club_model);
    CHECK(near(club.ball_position, own.ball_position, 0.001f));
    CHECK(near(club.aim_angle, own.aim_angle, 0.001f));
    CHECK(near(club.face_turn, own.face_turn, 0.001f));
    CHECK(near(club.shaft_lean, own.shaft_lean, 0.001f));
    CHECK(club.putt == own.putt);
    CHECK(club.raise == own.raise);
    // Not the stock setup: the face open (turned right of the path for a
    // right-hander), the shaft leaning back with the ball forward, the
    // stance moved.
    CHECK(club.face_turn < -glm::radians(3.0f));
    CHECK(club.shaft_lean < 0.0f);
    CHECK(near(club.aim_angle, golfer.address.setup.aim_angle, 0.001f));
    CHECK(!near(shown.position, address_stance_position(golfer.area, stock, golfer.tuning), 0.01f));
}

TEST_CASE("a refusal from the server shows as a notice") {
    game_state state = started_hole();
    state.notice = game_notice{"online.error.too_far", 0.0f, message_severity::error};
    CHECK(frame_for(state).notice_label == "TOO FAR AWAY");
}

TEST_CASE("the course map numbers every hole of a hub course along its line of play and fits to the holes") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    CHECK(frame_for(state).map_holes.empty());

    state.course_map_active = true;
    const render_data data = frame_for(state);
    REQUIRE(data.course_map_outline.size() >= 3U);
    glm::vec3 outline_low(1.0e9f);
    glm::vec3 outline_high(-1.0e9f);
    for (const glm::vec3& point : data.course_map_outline) {
        outline_low = glm::min(outline_low, point);
        outline_high = glm::max(outline_high, point);
    }
    REQUIRE(data.map_holes.size() == state.course_holes.size());
    for (std::size_t i = 0; i < data.map_holes.size(); ++i) {
        CHECK(data.map_holes[i].number == std::to_string(i + 1));
        REQUIRE(data.map_holes[i].line_of_play.size() == state.area.signs[i].line_of_play.size());
        for (std::size_t p = 0; p < data.map_holes[i].line_of_play.size(); ++p) {
            const glm::vec3 point = data.map_holes[i].line_of_play[p];
            CHECK(near(point, state.area.signs[i].line_of_play[p]));
            CHECK((point.x >= outline_low.x && point.x <= outline_high.x));
            CHECK((point.z >= outline_low.z && point.z <= outline_high.z));
        }
    }
    // The holes, not the land around them.
    CHECK(outline_high.x - outline_low.x <= state.area.extent * 2.0f);
    CHECK(outline_high.z - outline_low.z <= state.area.extent * 2.0f);
}

TEST_CASE("what is pinned to the camera keeps its tuned field of view whatever the setting") {
    game_state state = started_hole();
    const float tuned = state.tuning.camera.viewmodel_fov_degrees;
    REQUIRE(tuned > 0.0f);
    for (const float setting : {50.0f, 60.0f, 90.0f}) {
        const render_data data = frame_for(state, setting);
        CHECK(data.camera_fov_degrees == setting);
        CHECK(data.viewmodel_fov_degrees == tuned);
    }
}

namespace {
// The centre of each unit sphere among `vertices` (prop_batch's, in order).
std::vector<glm::vec3> sphere_centres(const std::vector<prop_vertex>& vertices) {
    const std::size_t per_sphere = prop_mesh_vertex_count(prop_mesh::sphere);
    std::vector<glm::vec3> centres;
    for (std::size_t first = 0; first + per_sphere <= vertices.size(); first += per_sphere) {
        glm::vec3 sum(0.0f);
        for (std::size_t i = first; i < first + per_sphere; ++i) {
            sum += vertices[i].position;
        }
        centres.push_back(sum / static_cast<float>(per_sphere));
    }
    return centres;
}

held_use held(const char* model, const float elapsed) {
    return held_use{&test_props().at(model), elapsed};
}
}

TEST_CASE("what I hold follows the camera: the same on screen wherever I look, the smoke rising up the world and the beer falling down it") {
    game_state state = started_game(fixture_hub_course());
    const float max_pitch = state.tuning.player.max_look_pitch;
    REQUIRE(max_pitch > 0.5f);
    struct seen {
        prop_batch batch;
        glm::mat4 view{1.0f};
        glm::vec3 tip{0.0f};
    };
    const auto props_seen = [&state] {
        const render_data data = frame_for(state);
        const prop_holder mine = camera_prop_holder(data.camera_position, data.camera_target, held("cigarette", 0.7f),
                                                    held("beer_can", 1.0f));
        seen view;
        append_held_props(view.batch, mine, test_scene_colors().glow);
        view.view = glm::lookAt(data.camera_position, data.camera_target, world_up);
        view.tip = glm::vec3(mine.frame * glm::vec4(mine.grip.smokable_tip, 1.0f));
        return view;
    };

    const seen level = props_seen();
    REQUIRE(!level.batch.opaque_vertices().empty());
    // Five puffs of smoke, then the beer's drops.
    const std::vector<glm::vec3> level_blobs = sphere_centres(level.batch.blended_vertices());
    REQUIRE(level_blobs.size() > 5U);
    float lowest_held = 1e9f;
    for (const prop_vertex& vertex : level.batch.opaque_vertices()) {
        lowest_held = std::min(lowest_held, vertex.position.y);
    }
    // Poured: the stream runs below the props held in view.
    CHECK(level_blobs.back().y < lowest_held);

    const auto check_same_on_screen = [&](const seen& now) {
        const std::vector<prop_vertex> placed_now = now.batch.opaque_vertices();
        const std::vector<prop_vertex> placed_level = level.batch.opaque_vertices();
        REQUIRE(placed_now.size() == placed_level.size());
        for (std::size_t i = 0; i < placed_now.size(); ++i) {
            const glm::vec3 seen_now(now.view * glm::vec4(placed_now[i].position, 1.0f));
            const glm::vec3 seen_level(level.view * glm::vec4(placed_level[i].position, 1.0f));
            CHECK(near(seen_now, seen_level, 0.0001f));
        }
        const std::vector<glm::vec3> blobs = sphere_centres(now.batch.blended_vertices());
        REQUIRE(blobs.size() == level_blobs.size());
        for (std::size_t i = 0; i < blobs.size(); ++i) {
            if (i < 5) {
                // A puff of smoke: over the tip in the world, as high as when looking level.
                CHECK(near(blobs[i].y - now.tip.y, level_blobs[i].y - level.tip.y, 0.0001f));
                CHECK(blobs[i].y > now.tip.y);
            } else {
                // A drop of beer: as far down the stream in the world as when looking level.
                CHECK(near(blobs[i].y - blobs[5].y, level_blobs[i].y - level_blobs[5].y, 0.0001f));
            }
        }
    };

    for (const float pitch : {-0.9f * max_pitch, 0.9f * max_pitch}) {
        state.look.pitch = pitch;
        check_same_on_screen(props_seen());
    }
    // Turned, hopping.
    state.look.pitch = -0.3f;
    state.player.yaw += 2.0f;
    state.player.position.y += 0.4f;
    check_same_on_screen(props_seen());

    // Sat in a cart with my head turned from its heading.
    state.ride = cart_ride{};
    state.player.position = anchor_on_terrain(state.area, seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart));
    state.player.yaw = state.cart.pose.yaw;
    state.look.head_yaw = 1.6f;
    state.look.pitch = 0.4f;
    check_same_on_screen(props_seen());
}

TEST_CASE("my beer can tips towards me to pour, its opening facing me and the beer in view") {
    game_state state = started_game(fixture_hub_course());
    const render_data data = frame_for(state);
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    // The narrowest picture (wider windows only add width).
    const glm::mat4 projection = glm::perspective(glm::radians(data.viewmodel_fov_degrees), 4.0f / 3.0f, 0.05f, 100.0f);
    const auto in_view = [&](const glm::vec3& world) {
        const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
        return clip.w > 0.0f && std::abs(clip.x / clip.w) < 0.95f && std::abs(clip.y / clip.w) < 0.95f;
    };
    const glm::vec3 ahead = glm::normalize(data.camera_target - data.camera_position);

    const prop_holder pouring = camera_prop_holder(data.camera_position, data.camera_target, std::nullopt, held("beer_can", 1.0f));
    const glm::mat4 can = held_can_frame(pouring, 1.0f);
    const glm::vec3 opening(can * glm::vec4(test_props().at("beer_can").point("pour"), 1.0f));
    const glm::vec3 top = glm::normalize(glm::vec3(can[1]));
    CHECK(in_view(opening));
    CHECK(glm::dot(top, data.camera_position - opening) > 0.0f);
    // Tipped from how it is first held: its top leans towards me.
    const glm::vec3 held_top = glm::normalize(glm::vec3(held_can_frame(pouring, 0.0f)[1]));
    CHECK(glm::dot(top, ahead) < glm::dot(held_top, ahead) - 0.3f);

    prop_batch batch;
    append_held_props(batch, pouring, test_scene_colors().glow);
    const glm::vec3 beer = test_props().at("beer_can").color("beer");
    const std::vector<prop_vertex> drops = batch.blended_vertices();
    CHECK(std::any_of(drops.begin(), drops.end(),
                      [&](const prop_vertex& vertex) { return vertex.color == beer && in_view(vertex.position); }));
}

TEST_CASE("tracking noise eases in while paused and after a hole changes, never in play") {
    tape_style tape;
    tape.tracking_burst_seconds = 1.0f;
    tape.tracking_fade_seconds = 0.5f;
    const auto run = [&tape](tape_tracking_state state, const tape_tracking_signals& signals, const float seconds) {
        for (float t = 0.0f; t < seconds; t += 0.05f) {
            state = update_tape_tracking(state, signals, 0.05f, tape);
        }
        return state;
    };

    const tape_tracking_state paused = run({}, {true, false, false}, 0.25f);
    CHECK(paused.level > 0.3f);
    CHECK(paused.level < 0.7f);
    CHECK(run(paused, {true, false, false}, 1.0f).level == 1.0f);
    CHECK(run(paused, {false, false, false}, 1.0f).level == 0.0f);
    CHECK(update_tape_tracking(paused, {true, false, true}, 0.05f, tape).level == 0.0f);

    // Bus travel fades to black, then the new place starts with a burst.
    tape.travel_fade_seconds = 0.5f;
    const tape_tracking_state travelling = run({}, {false, false, false, true}, 0.6f);
    CHECK(travelling.fade == 1.0f);
    const tape_tracking_state arrived = update_tape_tracking(travelling, {false, false, false, false}, 0.05f, tape);
    CHECK(arrived.burst_left == 1.0f);
    CHECK(arrived.fade < 1.0f);
    CHECK(run(arrived, {false, false, false, false}, 0.6f).fade == 0.0f);

    // A hole starting (or ending) runs a burst, then it fades out.
    const tape_tracking_state started = update_tape_tracking({}, {false, true, false}, 0.05f, tape);
    CHECK(started.burst_left == tape.tracking_burst_seconds);
    CHECK(run(started, {false, true, false}, 0.6f).level == 1.0f);
    CHECK(run(started, {false, true, false}, 2.0f).level == 0.0f);
    CHECK(run({}, {false, false, false}, 1.0f).level == 0.0f);
}

TEST_CASE("tracking signals: a hole is being played, and play means anything but walking") {
    game_state state = started_hole();
    state.mode = game_mode::walking;
    tape_tracking_signals signals = tape_tracking_signals_for(state, true);
    CHECK(signals.paused);
    CHECK(signals.on_hole);
    CHECK(!signals.in_play);
    state.mode = game_mode::following_shot;
    CHECK(tape_tracking_signals_for(state, false).in_play);
}

TEST_CASE("a tipped cart is seen lying on the side it rolled onto, a sunk one resting on its pond's bed") {
    game_state state = started_game(fixture_hub_course());
    // Standing behind my cart, looking along it.
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    state.player.position = anchor_on_terrain(state.area, state.cart.pose.position - ahead * 8.0f);
    state.player.yaw = state.cart.pose.yaw;
    state.look.pitch = 0.0f;
    const prop_model& model = test_props().at("golf_cart");
    const glm::vec3 roof_local(0.0f, 2.0f, 0.0f);

    for (const cart_condition side : {cart_condition::on_left_side, cart_condition::on_right_side}) {
        state.cart.motion.condition = side;
        const render_data data = frame_for(state);
        REQUIRE(data.carts.size() == 1U);
        const render_cart& drawn = data.carts[0];
        CHECK(!drawn.driven);
        const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
        const glm::mat4 projection = glm::perspective(glm::radians(test_fov_degrees), 4.0f / 3.0f, 0.05f, 500.0f);
        const auto screen_x = [&](const glm::vec3& world) {
            const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
            return clip.x / clip.w;
        };
        // Its roof lies over to that side of it on screen (+x is the screen's right).
        const glm::vec3 roof(cart_frame(drawn) * glm::vec4(roof_local, 1.0f));
        const float roof_x = screen_x(roof) - screen_x(state.cart.pose.position);
        CHECK((side == cart_condition::on_left_side ? roof_x < -0.05f : roof_x > 0.05f));
        // Lying on the ground, not in it nor floating: its lowest point just over it.
        prop_batch batch;
        append_cart_model(batch, drawn, model, test_scene_colors());
        float lowest = 1000.0f;
        for (const prop_vertex& vertex : batch.opaque_vertices()) {
            lowest = std::min(lowest, vertex.position.y - terrain_height(state.area, vertex.position));
        }
        CHECK(lowest > -0.15f);
        CHECK(lowest < 0.15f);
    }

    // Sunk in a pond shallower than it is tall: on its wheels on the bed, its
    // lower half under the water and its top above it, never through the ground.
    state.cart.motion.condition = cart_condition::sunk;
    state.area.zones.push_back(circle_zone(material_zone_type::water, state.cart.pose.position, 8.0f));
    const float surface = state.cart.pose.position.y + 0.6f;
    state.area.water_levels.push_back(surface);
    const render_data data = frame_for(state);
    REQUIRE(data.carts.size() == 1U);
    CHECK(near(data.carts[0].up, world_up, 0.05f));
    prop_batch batch;
    append_cart_model(batch, data.carts[0], model, test_scene_colors());
    float lowest = 1000.0f;
    float highest = -1000.0f;
    for (const prop_vertex& vertex : batch.opaque_vertices()) {
        lowest = std::min(lowest, vertex.position.y - terrain_height(state.area, vertex.position));
        highest = std::max(highest, vertex.position.y);
    }
    CHECK(lowest > -0.05f);
    CHECK(lowest < 0.15f);
    CHECK(highest > surface);
}

namespace {
// The picture's lines (assets/picture/picture.json ui_lines and the first scene_lines).
constexpr float scene_lines = 360.0f;

// Where `world` lands in the scene picture at `aspect`, in scene pixels from
// its middle (+x right, +y up); nothing when it is behind the eye.
std::optional<glm::vec2> scene_pixel(const render_data& data, const float aspect, const glm::vec3& world) {
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    const glm::mat4 projection = glm::perspective(glm::radians(data.camera_fov_degrees), aspect, 0.05f, 2000.0f);
    const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
    if (clip.w <= 0.0f) {
        return std::nullopt;
    }
    return glm::vec2(clip.x / clip.w * scene_lines * aspect, clip.y / clip.w * scene_lines) * 0.5f;
}

bool in_picture(const glm::vec2& pixel, const float aspect) {
    return std::abs(pixel.x) < 0.5f * scene_lines * aspect && std::abs(pixel.y) < 0.5f * scene_lines;
}

constexpr float aspects[] = {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f};
}

TEST_CASE("in the target view the line I aim along runs out ahead into the picture, for either hand") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        enter_addressing(state);
        const render_data data = frame_for(state);
        const glm::vec3 far = state.address.setup.ball_start + yaw_direction(state.address.setup.aim_angle) * 150.0f;
        for (const float aspect : aspects) {
            const std::optional<glm::vec2> seen = scene_pixel(data, aspect, far);
            REQUIRE(seen.has_value());
            CHECK(in_picture(*seen, aspect));
            // Near the middle across: the eyes are just to one side of the line.
            CHECK(std::abs(seen->x) < 0.1f * scene_lines);
        }
    }
}

TEST_CASE("the down view shows the actual club face and ball position without setup guide lines") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        state.selected_club = static_cast<std::size_t>(std::find_if(state.clubs.begin(), state.clubs.end(),
            [](const club_definition& club) { return club.id == "seven_iron"; }) - state.clubs.begin());
        enter_addressing(state);
        state.address.look = down_look(state.tuning.camera);
        const render_data data = frame_for(state);
        REQUIRE(data.my_club.has_value());
        REQUIRE(data.address_feet.has_value());
        const render_address_feet& feet = *data.address_feet;
        const auto face = [&](const render_data& shown) {
            const render_club_pose& pose = *shown.my_club;
            const prop_model& model = test_props().at(pose.club_model);
            for (const prop_piece& piece : prop_pieces(model, swing_club_frame(pose, shown.ball_visual_radius_meters, model),
                                                       club_head_pose(model, pose.loft_degrees))) {
                if (piece.part->shape == prop_shape::panel && piece.part->color == model.color("face")) {
                    return piece.model;
                }
            }
            CHECK(false);
            return glm::mat4(1.0f);
        };
        const glm::mat4 square_face = face(data);
        const glm::vec3 toe = glm::vec3(square_face * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f));
        const glm::vec3 heel = glm::vec3(square_face * glm::vec4(1.0f, -1.0f, 0.0f, 1.0f));
        // Only the two shoes remain in the address marker batch.
        world_marker_batch shoes;
        append_address_feet(shoes, feet, test_scene_colors());
        CHECK(shoes.vertices().size() == 2 * prop_mesh_vertex_count(prop_mesh::sphere));
        for (const float turn : {-1.0f, 1.0f}) {
            game_state turned = state;
            turned.address.setup.face_open += glm::radians(5.0f) * turn;
            const render_data turned_data = frame_for(turned);
            const glm::vec3 turned_toe = glm::vec3(face(turned_data) * glm::vec4(-1.0f, -1.0f, 0.0f, 1.0f));
            for (const float aspect : aspects) {
                for (const glm::vec3& point : {data.ball_position, toe, heel, feet.lead_foot, feet.trail_foot}) {
                    const std::optional<glm::vec2> seen = scene_pixel(data, aspect, point);
                    REQUIRE(seen.has_value());
                    CHECK(in_picture(*seen, aspect));
                }
                const glm::vec2 moved = *scene_pixel(data, aspect, turned_toe) - *scene_pixel(data, aspect, toe);
                const glm::vec2 ball = *scene_pixel(data, aspect, data.ball_position);
                const glm::vec2 target_side =
                    *scene_pixel(data, aspect, data.ball_position + yaw_direction(state.address.setup.aim_angle) * 0.3f) - ball;
                CHECK(glm::length(moved) >= 1.0f);
                CHECK(glm::dot(moved, target_side) * turn < 0.0f);
            }
        }
        for (const float position : {-1.0f, 1.0f}) {
            game_state moved = state;
            moved.address.setup.ball_position += state.tuning.setup.ball_position_step * position;
            const render_data moved_data = frame_for(moved);
            for (const float aspect : aspects) {
                const glm::vec2 ball = *scene_pixel(data, aspect, data.ball_position);
                const glm::vec2 ball_moved = *scene_pixel(moved_data, aspect, data.ball_position) - ball;
                const glm::vec2 target_side =
                    *scene_pixel(data, aspect, data.ball_position + yaw_direction(state.address.setup.aim_angle) * 0.3f) - ball;
                CHECK(glm::length(ball_moved) >= 2.0f);
                CHECK(glm::dot(ball_moved, target_side) * position > 0.0f);
            }
        }
    }
}

TEST_CASE("iron and wedge heads show their own loft while carried, handled and soled at the ball") {
    game_state state = started_hole();
    const prop_model& model = test_props().at("club_iron");
    const auto panel_in = [&](const glm::mat4& frame, const float loft) {
        for (const prop_piece& piece : prop_pieces(model, frame, club_head_pose(model, loft))) {
            if (piece.part->shape == prop_shape::panel && piece.part->color == model.color("face")) {
                return piece.model;
            }
        }
        CHECK(false);
        return glm::mat4(1.0f);
    };
    std::vector<glm::vec2> projected_edges;
    for (const char* id : {"five_iron", "lob_wedge"}) {
        const auto found = std::find_if(state.clubs.begin(), state.clubs.end(),
                                        [id](const club_definition& club) { return club.id == id; });
        REQUIRE(found != state.clubs.end());
        state.selected_club = static_cast<std::size_t>(found - state.clubs.begin());
        state.mode = game_mode::walking;
        render_data walking = frame_for(state);
        REQUIRE(walking.my_figure.has_value());
        CHECK(walking.my_figure->carried_club_loft_degrees == found->spec.static_loft_degrees);
        // The normal of the actual face panel inclines skywards by its spec,
        // whichever pose holds the shaft; the shaft itself is not lofted.
        const glm::mat4 carried = carried_club_frame(*walking.my_figure, walking.avatar_eye_height, model);
        const glm::vec3 carried_normal = glm::normalize(glm::vec3(panel_in(carried, found->spec.static_loft_degrees)[2]));
        const glm::vec3 local_normal = glm::vec3(glm::inverse(carried) * glm::vec4(carried_normal, 0.0f));
        CHECK(near(glm::degrees(std::atan2(local_normal.y, local_normal.z)), found->spec.static_loft_degrees, 0.001f));
        bag_handling handling;
        handling.kind = handling_kind::club;
        handling.from = state.bag;
        handling.to = state.bag;
        handling.club_out = "driver";
        handling.club_in = id;
        handling.elapsed = state.tuning.player.club_swap_seconds * 0.75f;
        state.handling = handling;
        const render_data swapping = frame_for(state);
        REQUIRE(swapping.handled_props.size() == 1U);
        CHECK(swapping.handled_props.front().club_loft_degrees == found->spec.static_loft_degrees);
        state.handling.reset();
        enter_addressing(state);
        state.address.look = down_look(state.tuning.camera);
        const render_data address = frame_for(state);
        REQUIRE(address.my_club.has_value());
        const render_club_pose& pose = *address.my_club;
        CHECK(pose.loft_degrees == found->spec.static_loft_degrees);
        const glm::mat4 frame = swing_club_frame(pose, address.ball_visual_radius_meters, model);
        const glm::mat4 face = panel_in(frame, pose.loft_degrees);
        const glm::vec3 face_at(face[3]);
        const glm::vec3 normal = glm::normalize(glm::vec3(face[2]));
        CHECK(normal.y > 0.0f);
        CHECK(near(glm::dot(address.ball_position - face_at, normal), address.ball_visual_radius_meters, 0.001f));
        CHECK(near(swing_club_grip(pose, address.ball_visual_radius_meters),
                   glm::vec3(frame * glm::vec4(model.point("grip"), 1.0f)), 0.0001f));
        prop_batch batch;
        append_swing_club(batch, pose, address.ball_visual_radius_meters, model, test_scene_colors().glow);
        float lowest = batch.opaque_vertices().front().position.y;
        for (const prop_vertex& vertex : batch.opaque_vertices()) {
            lowest = std::min(lowest, vertex.position.y);
        }
        CHECK(std::abs(lowest - (address.ball_position.y - address.ball_visual_radius_meters)) < 0.005f);
        const glm::vec3 bottom(face * glm::vec4(0.0f, -1.0f, 0.0f, 1.0f));
        const glm::vec3 top(face * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
        const auto bottom_pixel = scene_pixel(address, 16.0f / 9.0f, bottom);
        const auto top_pixel = scene_pixel(address, 16.0f / 9.0f, top);
        REQUIRE(bottom_pixel.has_value());
        REQUIRE(top_pixel.has_value());
        CHECK(in_picture(*bottom_pixel, 16.0f / 9.0f));
        CHECK(in_picture(*top_pixel, 16.0f / 9.0f));
        projected_edges.push_back(*top_pixel - *bottom_pixel);
    }
    REQUIRE(projected_edges.size() == 2U);
    CHECK(glm::length(projected_edges[1] - projected_edges[0]) > 2.0f);
}

TEST_CASE("a left-hander stands, looks and opens the face mirrored, and the physics sees world angles") {
    game_state right = started_hole();
    game_state left = started_hole();
    left.hand = handedness::left;
    enter_addressing(right);
    enter_addressing(left);
    const glm::vec3 ball = right.address.setup.ball_start;
    const glm::vec3 forward = yaw_direction(right.address.setup.aim_angle);
    const glm::vec3 side = yaw_left(forward);
    // Stances and eyes either side of the line, the same distance out.
    const float right_out = glm::dot(right.player.position - ball, side);
    const float left_out = glm::dot(left.player.position - ball, side);
    CHECK(right_out > 0.0f);
    CHECK(near(left_out, -right_out, 0.001f));
    CHECK(glm::dot(my_address_eye(right).position - ball, side) > 0.0f);
    CHECK(glm::dot(my_address_eye(left).position - ball, side) < 0.0f);
    // Both target views look the same way down the line.
    CHECK(near(horizontal(my_address_eye(right).direction), horizontal(my_address_eye(left).direction), 0.0001f));
    // Up, looking down, opens either face: a right-hander's to the right of the path, a left-hander's to the left.
    right.address.look = down_look(right.tuning.camera);
    left.address.look = down_look(left.tuning.camera);
    game_input up;
    up.look_up_held = true;
    update_game(right, up, 0.016f);
    update_game(left, up, 0.016f);
    CHECK(wrap_angle(world_face_yaw(right.address.setup) - right.address.setup.aim_angle) < 0.0f);
    CHECK(wrap_angle(world_face_yaw(left.address.setup) - left.address.setup.aim_angle) > 0.0f);
    // The swing's club and figure are drawn mirrored too.
    const render_data right_frame = frame_for(right);
    const render_data left_frame = frame_for(left);
    REQUIRE(right_frame.my_club.has_value());
    REQUIRE(left_frame.my_club.has_value());
    CHECK(!right_frame.my_club->left_handed);
    CHECK(left_frame.my_club->left_handed);
    CHECK(right_frame.my_club->face_turn < 0.0f);
    CHECK(left_frame.my_club->face_turn > 0.0f);
}

TEST_CASE("over the ball the frame shows the key hints until the swing, then the swipe's meter") {
    game_state state = started_hole();
    address_looking_down(state);
    render_data data = frame_for(state);
    REQUIRE(data.key_hints != nullptr);
    CHECK(std::string(data.key_hints) == text_hud_address_hints);
    CHECK(data.show_swing_clock);
    CHECK(data.swing_backswing == state.address.setup.backswing);
    CHECK(!data.swing_meter);

    // The swipe's meter takes the club back slowly, under the dialled top.
    update_game(state, swing_input(), 0.016f);
    update_game(state, game_input{}, 0.5f);
    data = frame_for(state);
    CHECK(data.key_hints == nullptr);
    REQUIRE(data.swing_meter.has_value());
    CHECK(*data.swing_meter > 0.0f);
    CHECK(*data.swing_meter < data.swing_backswing);
    REQUIRE(data.my_club.has_value());
    CHECK(near(data.my_club->raise, *data.swing_meter, 0.0001f));
    CHECK(!data.swing_mistimed);

    // Waiting at the top, the club holds there but the meter rises on, red;
    // the downswing started late stays red.
    const float waited = state.tuning.swing.meter_full_seconds + state.tuning.swing.timing_window_seconds;
    for (float elapsed = 0.0f; elapsed < waited; elapsed += 0.05f) {
        update_game(state, game_input{}, 0.05f);
    }
    data = frame_for(state);
    REQUIRE(data.swing_meter.has_value());
    CHECK(*data.swing_meter > data.swing_backswing);
    CHECK(data.swing_mistimed);
    REQUIRE(data.my_club.has_value());
    CHECK(near(data.my_club->raise, data.swing_backswing, 0.0001f));
    game_input left;
    left.swipe = -1.0f;
    update_game(state, left, 0.016f);
    REQUIRE(state.address.swing_elapsed.has_value());
    CHECK(frame_for(state).swing_mistimed);

    // Swiped left early, it is red through the downswing too.
    game_state early = started_hole();
    address_looking_down(early);
    update_game(early, swing_input(), 0.016f);
    update_game(early, game_input{}, 0.1f);
    update_game(early, left, 0.016f);
    REQUIRE(early.address.swing_elapsed.has_value());
    CHECK(frame_for(early).swing_mistimed);
}

TEST_CASE("the ball is drawn right over where it is, near a cup too, and the cup as big as it plays") {
    game_state state = started_hole();
    const cup& hole = state.area.cups[*state.ball->hole];
    const world_scale_tuning& scale = state.tuning.scale;
    for (const float out : {0.0f, 0.5f * hole.radius, hole.radius, 0.3f, 2.0f}) {
        const glm::vec3 at = hole.centre + glm::vec3(out, 0.0f, 0.0f);
        const glm::vec3 drawn = drawn_ball_center(at, scale);
        CHECK(near(horizontal(drawn), horizontal(at)));
        // Its bottom where the real ball's is.
        CHECK(near(drawn.y - scale.ball_visual_radius_meters, at.y - scale.ball_physics_radius_meters, 0.0001f));
    }
    const render_data data = frame_for(state);
    CHECK(near(data.cup_radius_meters, hole.radius * scale.meters_per_world_unit, 0.0001f));
    CHECK(near(data.cup_depth_meters, hole.depth * scale.meters_per_world_unit, 0.0001f));
    // The drawn ball fits the drawn cup with room to drop, as a real one does.
    CHECK(scale.ball_visual_radius_meters < 0.6f * data.cup_radius_meters);
    CHECK(2.0f * scale.ball_visual_radius_meters < data.cup_depth_meters);

    // Rolling over the green near the cup, and riding its rim, the drawn
    // ball is right over the real one.
    const std::vector<cup>& cups = state.area.cups;
    const float real = scale.ball_physics_radius_meters / scale.meters_per_world_unit;
    const float drawn_radius = scale.ball_visual_radius_meters / scale.meters_per_world_unit;
    for (const glm::vec3& at : {hole.centre + glm::vec3(0.3f, real, 0.0f), hole.centre + glm::vec3(hole.radius, real, 0.0f)}) {
        CHECK(near(drawn_ball_center(at, scale, cups), drawn_ball_center(at, scale)));
    }
    // Sinking down against the wall, what of it is below the rim stays
    // inside the hole, and it ends on the bottom where the real one does.
    for (const float sunk : {0.0f, 0.01f, 0.03f, 0.06f, hole.depth - 2.0f * real}) {
        const glm::vec3 at = hole.centre + glm::vec3(0.0f, real - sunk, hole.radius - real);
        const glm::vec3 drawn = drawn_ball_center(at, scale, cups);
        CHECK(near(drawn.y, drawn_ball_center(at, scale).y));
        const float below = std::clamp(hole.centre.y - (drawn.y - drawn_radius), 0.0f, drawn_radius);
        const float half_width = std::sqrt(below * (2.0f * drawn_radius - below));
        CHECK(horizontal_distance(drawn, hole.centre) + half_width <= hole.radius + 0.0001f);
        CHECK(horizontal_distance(drawn, hole.centre) <= horizontal_distance(at, hole.centre) + 0.0001f);
    }
    // Resting on the bottom, the whole drawn ball is inside the wall.
    const glm::vec3 resting = drawn_ball_center(hole.centre + glm::vec3(0.0f, real - hole.depth, hole.radius - real), scale, cups);
    CHECK(horizontal_distance(resting, hole.centre) + drawn_radius <= hole.radius + 0.0001f);
}

TEST_CASE("with the putter the preview is a start line on the green along where the ball starts, not an arc") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        game_state state = started_hole();
        state.hand = hand;
        state.selected_club = state.clubs.size() - 1;
        REQUIRE(state.clubs[state.selected_club].club_class == "putter");
        enter_addressing(state);
        render_data data = frame_for(state);
        const std::vector<glm::vec3>& putt_line = data.osd_line;
        REQUIRE(putt_line.size() >= 2U);
        const shot_setup& setup = state.address.setup;
        const shot_preview_tuning& tuning = state.tuning.shot_preview;
        const float unit = state.tuning.scale.meters_per_world_unit;
        CHECK(near(horizontal(putt_line.front()), horizontal(setup.ball_start), 0.0001f));
        CHECK(near(horizontal_distance(putt_line.front(), putt_line.back()), tuning.putt_line_meters / unit, 0.001f));
        for (const glm::vec3& point : putt_line) {
            CHECK(near(point.y, terrain_height(state.area, point), 0.0001f));
        }
        // Square, straight along the aim.
        const float square = yaw_towards(putt_line.front(), putt_line.back());
        CHECK(near(wrap_angle(square - setup.aim_angle), 0.0f, 0.001f));

        // Opened, it starts the way the face points: right for a right-hander, left for a left-hander.
        state.address.look = down_look(state.tuning.camera);
        game_input open;
        open.look_up_held = true;
        for (int i = 0; i < 30; ++i) {
            update_game(state, open, 0.016f);
        }
        data = frame_for(state);
        const float turned = wrap_angle(yaw_towards(data.osd_line.front(), data.osd_line.back()) - state.address.setup.aim_angle);
        CHECK((hand == handedness::right ? turned < -0.01f : turned > 0.01f));
        CHECK(std::abs(turned) < std::abs(wrap_angle(world_face_yaw(state.address.setup) - state.address.setup.aim_angle)));
    }
    // Every other club shows the start of its flight, rising off the ground.
    game_state iron = started_hole();
    enter_addressing(iron);
    const render_data data = frame_for(iron);
    REQUIRE(data.osd_line.size() > 2U);
    CHECK(data.osd_line.back().y > data.osd_line.front().y);
}

TEST_CASE("my bag is drawn where it is: behind my cart's driver's seat, stood on the ground leant back, on my back, and on the map") {
    game_state state = started_hole();
    state.course_map_active = true;
    render_data data = frame_for(state);
    REQUIRE(!data.carts.empty());
    const render_cart& cart = data.carts[0];
    REQUIRE(cart.bags.count("driver_bag") == 1U);
    CHECK(data.my_figure->bag_model.empty());
    const prop_model& bag = test_props().at(cart.bags.at("driver_bag").model);
    const prop_model& cart_model = test_props().at(cart.model);
    const glm::mat4 strapped = mounted_frame(cart_frame(cart), cart_model.mount("driver_bag"), bag);
    const glm::vec3 bottom(strapped * glm::vec4(bag.point("hang"), 1.0f));
    const glm::vec3 top(strapped * glm::vec4(bag.point("hang") + glm::vec3(0.0f, 0.8f, 0.0f), 1.0f));
    const glm::vec3 forward = yaw_direction(cart.yaw);
    // Behind the seat, on the driver's (left) side, standing up and leaning back.
    CHECK(glm::dot(bottom - cart.position, forward) < -0.4f);
    CHECK(glm::dot(bottom - cart.position, yaw_left(forward)) > 0.1f);
    CHECK(top.y > bottom.y + 0.6f);
    CHECK(glm::dot(top - bottom, forward) < 0.0f);
    REQUIRE(data.map_bag_position.has_value());
    CHECK(horizontal_distance(*data.map_bag_position, bag_position(state)) < 0.001f);

    // On my back: no bag on the cart or the map, and it is slung on my figure.
    state.player.position = bag_position(state);
    game_input b;
    b.move_bag = true;
    update_game(state, b, 0.016f);
    REQUIRE(bag_on_back(state));
    finish_handling(state);
    state.course_map_active = true;
    data = frame_for(state);
    CHECK(data.carts[0].bags.empty());
    CHECK(!data.map_bag_position.has_value());
    REQUIRE(data.my_figure.has_value());
    CHECK(data.my_figure->bag_model == "golf_bag");
    const glm::mat4 slung = back_bag_frame(*data.my_figure, data.avatar_eye_height, bag);
    const glm::vec3 slung_bottom(slung * glm::vec4(bag.point("hang"), 1.0f));
    CHECK(glm::dot(slung_bottom - state.player.position, yaw_direction(state.player.yaw)) < 0.0f);

    // Set down away from the cart: standing on the ground beside me, leant back from its pocket.
    state.player.position += glm::vec3(0.0f, 0.0f, 12.0f);
    state.player.position.y = terrain_height(state.area, state.player.position);
    update_game(state, b, 0.016f);
    REQUIRE(state.bag.where == bag_where::on_ground);
    finish_handling(state);
    data = frame_for(state);
    REQUIRE(data.ground_bags.size() == 1U);
    const render_ground_bag& down = data.ground_bags[0];
    const glm::mat4 stood = ground_bag_frame(down, bag);
    const glm::vec3 foot(stood * glm::vec4(bag.point("hang"), 1.0f));
    const glm::vec3 head(stood * glm::vec4(bag.point("hang") + glm::vec3(0.0f, 0.8f, 0.0f), 1.0f));
    CHECK(near(foot.y, terrain_height(state.area, foot), 0.05f));
    CHECK(head.y > foot.y + 0.7f);
    CHECK(glm::dot(head - foot, yaw_direction(down.yaw)) < 0.0f);
}

TEST_CASE("a water reflection contains my physical figure through the shared world path") {
    auto state=started_hole();
    step_up_to_ball(state);
    auto scene=frame_for(state);
    scene.smoking=render_item_use{"cigarette",1};
    scene.drinking=render_item_use{"beer",2};
    REQUIRE(scene.my_figure.has_value());
    REQUIRE(scene.my_club.has_value());
    const glm::vec3 eye(scene.camera_position.x,-scene.camera_position.y,scene.camera_position.z);
    const auto reflected=make_reflected_scene(scene,eye);
    CHECK(reflected.camera_position==eye);
    REQUIRE(reflected.remote_avatars.size()==scene.remote_avatars.size()+1);
    CHECK(reflected.remote_avatars.back().position==scene.my_figure->position);
    CHECK(reflected.remote_avatars.back().yaw==scene.my_figure->yaw);
    CHECK(reflected.remote_avatars.back().swing.has_value());
    REQUIRE(reflected.remote_avatars.back().smoking.has_value());
    REQUIRE(reflected.remote_avatars.back().drinking.has_value());
    CHECK(reflected.remote_avatars.back().smoking->model==scene.smoking->model);
    CHECK(reflected.remote_avatars.back().drinking->elapsed==scene.drinking->elapsed);
    CHECK(!reflected.my_club.has_value());
    CHECK(!reflected.address_feet.has_value());
    CHECK(reflected.trees==scene.trees);
    CHECK(reflected.terrain_mesh==scene.terrain_mesh);
    CHECK(scene.my_club.has_value());
}

namespace {
using vertex_key = std::array<float, 7>;

std::vector<vertex_key> sorted_keys(const prop_batch& batch, const bool blended) {
    std::vector<vertex_key> keys;
    for (const prop_vertex& v : blended ? batch.blended_vertices() : batch.opaque_vertices()) {
        keys.push_back({v.position.x, v.position.y, v.position.z, v.normal.x, v.color.x, v.alpha, v.glow});
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

std::vector<vertex_key> sorted_keys(const prop_frame& frame, const prop_view view, const bool blended) {
    const prop_ranges ranges = frame.ranges(view, blended);
    const canonical_meshes meshes;
    std::vector<vertex_key> keys;
    // Each run's instances, every vertex of the canonical mesh as the instance places it.
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const prop_runs& runs = ranges[static_cast<std::size_t>(mesh)];
        for (int i = 0; i < runs.count; ++i) {
            const prop_range run = runs.run[static_cast<std::size_t>(i)];
            for (std::size_t n = run.first; n < run.first + run.count; ++n) {
                const mesh_instance& instance = frame.batch().instances(blended, mesh)[n];
                for (std::size_t v = 0; v < meshes.vertex_count(mesh); ++v) {
                    const glm::vec3 position = placed_position(instance, meshes, mesh, v);
                    keys.push_back({position.x, position.y, position.z, placed_normal(instance, meshes, mesh, v).x, instance.color.x,
                                    instance.alpha, instance.glow});
                }
            }
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}
}

TEST_CASE("one prop upload serves the main view, the mirrored view and the sun's shadow with what each used to build") {
    game_state state = started_hole();
    step_up_to_ball(state);
    render_data data = frame_for(state);
    REQUIRE(data.my_figure.has_value());
    REQUIRE(data.my_club.has_value());
    render_remote_avatar other = *data.my_figure;
    other.player_id = 7;
    other.position += glm::vec3(3.0f, 0.0f, 0.0f);
    data.remote_avatars.push_back(other);
    const scene_colors& colors = test_scene_colors();
    prop_frame frame;
    frame.build(data, test_props(), colors, 0.0f);

    // What the main view built before: carts, bags, handled gear, my club, others' club and gear.
    const auto assemble = [&](const render_data& scene, const bool include_gear_only) {
        prop_batch expected;
        if (!include_gear_only) {
            append_carts_and_bags(expected, scene, test_props(), colors);
            if (scene.my_club) {
                append_swing_club(expected, *scene.my_club, scene.ball_visual_radius_meters,
                                  test_props().at(scene.my_club->club_model), colors.glow);
            }
            for (const render_remote_avatar& avatar : scene.remote_avatars) {
                if (avatar.swing) {
                    append_swing_club(expected, *avatar.swing, scene.ball_visual_radius_meters,
                                      test_props().at(avatar.swing->club_model), colors.glow);
                }
                if (const auto holder = remote_prop_holder(avatar, scene.avatar_eye_height, test_props())) {
                    append_held_props(expected, *holder, colors.glow);
                }
                append_figure_gear(expected, avatar, scene.avatar_eye_height, test_props(), colors);
            }
        } else {
            append_carts_and_bags(expected, scene, test_props(), colors);
            if (scene.my_figure) { append_figure_gear(expected, *scene.my_figure, scene.avatar_eye_height, test_props(), colors); }
            for (const render_remote_avatar& avatar : scene.remote_avatars) {
                append_figure_gear(expected, avatar, scene.avatar_eye_height, test_props(), colors);
            }
        }
        return expected;
    };
    for (const bool blended : {false, true}) {
        CHECK(sorted_keys(frame, prop_view::scene, blended) == sorted_keys(assemble(data, false), blended));
        // The mirror's scene is the world with my figure added as a player and my club and hand left out.
        render_data mirrored = make_reflected_scene(data, data.camera_position);
        CHECK(sorted_keys(frame, prop_view::mirror, blended) == sorted_keys(assemble(mirrored, false), blended));
        // The sun's shadow: carts, bags and the gear every figure carries, mine included.
        CHECK(sorted_keys(frame, prop_view::sun_shadow, blended) == sorted_keys(assemble(data, true), blended));
    }
    CHECK(!frame.batch().empty());
    // The scene view's groups are one run of each mesh's list.
    for (const prop_runs& runs : frame.ranges(prop_view::scene, false)) { CHECK(runs.count <= 1); }
}

namespace {
constexpr int pole_picture_width = 320;
constexpr int pole_picture_height = 240;
constexpr float pole_zoom = 8.0f;
constexpr float pole_height_meters = 2.13f;
constexpr float real_pole_radius = 0.00635f;

// A standing flagstick at the origin seen from `distance` metres down +Z and `sideways` off to one side.
struct pole_shot {
    std::vector<glm::vec2> pole;  // the pole's vertices on the picture, in pixels
    float radius = 0.0f;          // the radius the pole was drawn at
};

// The pole the view draws: the main one, or at `magnification` above 1 the zoomed view's own.
pole_shot shoot_pole(const float distance, const float sideways, const float magnification) {
    render_data data;
    data.show_hole = true;
    data.show_rangefinder = magnification > 1.0f;
    data.rangefinder_eyepiece.magnification = magnification;
    data.pin_visual_height_meters = pole_height_meters;
    data.flagstick_radius_meters = real_pole_radius;
    data.camera_position = glm::vec3(sideways, 1.7f, distance);
    data.camera_fov_degrees = test_fov_degrees;
    world_marker_batch batch;
    build_world_marker_batch(batch, marker_scene_for(data, pole_picture_height), test_scene_colors());
    const std::size_t first = magnification > 1.0f ? batch.part_end(static_cast<std::size_t>(marker_part::poles)) : 0;
    CHECK(batch.part_end(static_cast<std::size_t>(marker_part::zoom_poles)) - batch.part_end(static_cast<std::size_t>(marker_part::poles)) ==
            (magnification > 1.0f ? primitive_cylinder_segments * 12 : 0));
    const glm::vec3 target(0.0f, pole_height_meters * 0.5f, 0.0f);
    glm::mat4 proj = glm::perspective(glm::radians(test_fov_degrees),
                                      static_cast<float>(pole_picture_width) / pole_picture_height, 0.1f, 1000.0f);
    proj[0][0] *= magnification;  // the eyepiece's zoom (renderer/eyepiece_pass.cpp zoomed_projection)
    proj[1][1] *= magnification;
    const glm::mat4 view_proj = proj * glm::lookAt(data.camera_position, target, glm::vec3(0.0f, 1.0f, 0.0f));
    pole_shot shot;
    for (std::size_t i = first; i < first + primitive_cylinder_segments * 12; ++i) {
        const glm::vec3 position = batch.vertices()[i].position;
        shot.radius = std::max(shot.radius, glm::length(horizontal(position)));
        const glm::vec4 clip = view_proj * glm::vec4(position, 1.0f);
        shot.pole.emplace_back((clip.x / clip.w * 0.5f + 0.5f) * pole_picture_width,
                               (clip.y / clip.w * 0.5f + 0.5f) * pole_picture_height);
    }
    return shot;
}

float pole_width(const pole_shot& shot) {
    const auto [low, high] = std::minmax_element(shot.pole.begin(), shot.pole.end(),
                                                 [](const glm::vec2& a, const glm::vec2& b) { return a.x < b.x; });
    return high->x - low->x;
}

// Rows of the pole's height whose centre line holds no pixel centre inside the pole's triangles.
int uncovered_pole_rows(const pole_shot& shot) {
    float low = shot.pole.front().y;
    float high = low;
    for (const glm::vec2& p : shot.pole) {
        low = std::min(low, p.y);
        high = std::max(high, p.y);
    }
    int missed = 0;
    for (int row = static_cast<int>(std::ceil(low - 0.5f)); row + 0.5f <= high; ++row) {
        const float y = static_cast<float>(row) + 0.5f;
        bool covered = false;
        for (std::size_t t = 0; t + 2 < shot.pole.size() && !covered; t += 3) {
            float from = 1e9f;
            float to = -1e9f;
            for (std::size_t e = 0; e < 3; ++e) {
                const glm::vec2 a = shot.pole[t + e];
                const glm::vec2 b = shot.pole[t + (e + 1) % 3];
                if ((a.y - y) * (b.y - y) <= 0.0f && a.y != b.y) {
                    const float x = a.x + (b.x - a.x) * (y - a.y) / (b.y - a.y);
                    from = std::min(from, x);
                    to = std::max(to, x);
                }
            }
            covered = from <= to && std::ceil(from - 0.5f) <= std::floor(to - 0.5f);
        }
        missed += covered ? 0 : 1;
    }
    return missed;
}
}

TEST_CASE("a far flagstick is a whole pixel wide in each view, the eyepiece's own pixel in the zoomed one") {
    for (const float distance : {50.0f, 150.0f, 400.0f}) {
        for (int step = 0; step < 16; ++step) {
            const float sideways = static_cast<float>(step) / 16.0f * distance * 0.0048f;
            const pole_shot main = shoot_pole(distance, sideways, 1.0f);
            CHECK(pole_width(main) >= 1.0f - 0.001f);
            CHECK(main.radius > real_pole_radius);
            CHECK(uncovered_pole_rows(main) == 0);
            const pole_shot zoomed = shoot_pole(distance, sideways, pole_zoom);
            CHECK(pole_width(zoomed) >= 1.0f - 0.001f);
            CHECK(uncovered_pole_rows(zoomed) == 0);
            if (distance == 400.0f) {
                // Not the main view's width magnified, which would be about eight pixels.
                CHECK(pole_width(zoomed) <= 1.5f);
                CHECK(zoomed.radius < main.radius * 0.5f);
            }
        }
    }
}

TEST_CASE("a flagstick wider than a pixel of its view keeps its real radius") {
    for (const float distance : {1.0f, 1.5f}) {
        const pole_shot main = shoot_pole(distance, 0.0f, 1.0f);
        CHECK(pole_width(main) > 1.0f);
        CHECK(near(main.radius, real_pole_radius, 0.0001f));
    }
    // Far enough to be a pixel in the main view, but wider than one zoomed pixel.
    for (const float distance : {5.0f, 10.0f}) {
        const pole_shot zoomed = shoot_pole(distance, 0.0f, pole_zoom);
        CHECK(pole_width(zoomed) > 1.0f);
        CHECK(near(zoomed.radius, real_pole_radius, 0.0001f));
    }
}

namespace {
bool has_colour(const overlay_batch& batch, const glm::vec3 colour) {
    return std::any_of(batch.vertices.begin(), batch.vertices.end(), [&](const overlay_vertex& v) { return glm::vec3(v.color) == colour; });
}
// What a notice's batch looks like for `severity`: the three parts that make it red.
struct notice_look { bool error_fill, error_border, error_text, neutral_border; };
notice_look drawn_notice(const std::string& label, const message_severity severity, const overlay_grid grid, std::size_t& cut) {
    overlay_batch batch;
    batch.grid = grid;
    draw_notice(batch, shipped_text_assets(), label, severity);
    cut = batch.truncated_text_count;
    for (const overlay_vertex& v : batch.vertices) { CHECK(std::abs(v.position.x) <= 1.0f); CHECK(std::abs(v.position.y) <= 1.0f); }
    return {has_colour(batch, glm::vec3(0.03f, 0.02f, 0.02f)), has_colour(batch, glm::vec3(0.62f, 0.30f, 0.22f)),
            has_colour(batch, glm::vec3(0.94f, 0.38f, 0.26f)), has_colour(batch, glm::vec3(0.50f, 0.52f, 0.48f))};
}
}

TEST_CASE("only a refusal is drawn red: text, border and background; a status is neutral at every aspect") {
    for (const overlay_grid grid : {overlay_grid{640, 480}, overlay_grid{854, 480}, overlay_grid{1120, 480}}) {
        for (const char* key : {"online.error.too_far", "hub.pending", "scorecard.waiting_for_group", "hud.water_penalty", "hud.ball_hit_penalty"}) {
            const std::string label = lookup_text(shipped_text_assets(), key);
            std::size_t cut = 0;
            const notice_look error = drawn_notice(label, message_severity::error, grid, cut);
            CHECK(cut == 0U);
            CHECK(error.error_fill); CHECK(error.error_border); CHECK(error.error_text); CHECK(!error.neutral_border);
            const notice_look status = drawn_notice(label, message_severity::status, grid, cut);
            CHECK(cut == 0U);
            CHECK(!status.error_fill); CHECK(!status.error_border); CHECK(!status.error_text); CHECK(status.neutral_border);
        }
    }
}

TEST_CASE("the HUD notice carries the producer's severity; progress is neutral and never hides a refusal; travel has no HUD notice") {
    game_state state = started_hole();
    state.notice = game_notice{"online.error.too_far", 0.0f, message_severity::error};
    state.hub_flow.ball_request = net_command_type::tee_up;
    render_data data = frame_for(state);
    CHECK(data.notice_label == "TOO FAR AWAY");
    CHECK(data.notice_severity == message_severity::error);
    // Progress alone: neutral.
    state.notice.reset();
    data = frame_for(state);
    CHECK(data.notice_label == "Waiting for the server...");
    CHECK(data.notice_severity == message_severity::status);
    // A rules consequence keeps its notice, as a status.
    state.notice = game_notice{text_hud_water_penalty, 0.0f, message_severity::status};
    data = frame_for(state);
    CHECK(data.notice_label == "IN THE WATER: +1 STROKE");
    CHECK(data.notice_severity == message_severity::status);
    // Travel shows on its own screen only: no duplicate in the HUD.
    state.notice.reset(); state.hub_flow.ball_request.reset();
    state.hub_flow.travel_course = "somewhere";
    CHECK(frame_for(state).notice_label.empty());
}
