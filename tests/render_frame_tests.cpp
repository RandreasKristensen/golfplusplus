#include "doctest.h"

#include "core/render_frame.h"
#include "game/course_session.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"
#include "renderer/remote_avatar_batch.h"

#include "test_support.h"

namespace {
constexpr float test_fov_degrees = 60.0f;

render_data frame_for(const game_state& state, const float fov_degrees = test_fov_degrees) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, input_state{}, text, shipped_content().skills, render_meshes{}, fov_degrees);
}
}

TEST_CASE("a hole frame shows the ball, every tee box and the pin") {
    game_state state = started_hole();
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);

    CHECK(data.show_ball);
    CHECK(data.show_hole);
    CHECK(data.pin_position == state.static_anchors.pin_anchor);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == state.area.tee_boxes.size());
    CHECK(data.show_interact_prompt);
    CHECK(data.camera_fov_degrees == test_fov_degrees);
}

TEST_CASE("the drawn ball sits on the ground, not sunk into it") {
    game_state state = started_hole();
    const render_data data = frame_for(state);
    const float bottom = data.ball_position.y - data.ball_visual_radius_meters;
    CHECK(near(bottom, terrain_height(state.area, state.ball.position), 0.001f));
    CHECK(near(horizontal(data.ball_position), horizontal(state.ball.position)));
}

TEST_CASE("a hub frame hides the ball and shows every tee box, pin and claimable collectible") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(!data.show_ball);
    CHECK(!data.show_hole);
    REQUIRE(data.tee_boxes != nullptr);
    CHECK(data.tee_boxes->size() == 3U);
    CHECK(data.pin_markers.size() == 3U);
    // The fitness cache needs fitness level 2, so two of three are claimable.
    CHECK(data.collectible_markers.size() == 2U);
}

TEST_CASE("a round on a course world keeps every other hole's pin up") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 1));
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);
    CHECK(data.show_hole);
    CHECK(data.pin_position == state.static_anchors.pin_anchor);
    const std::vector<glm::vec3>& all = state.static_anchors.hub_pin_markers;
    REQUIRE(all.size() == 3U);
    REQUIRE(data.pin_markers.size() == 2U);
    CHECK(data.pin_markers[0] == all[0]);
    CHECK(data.pin_markers[1] == all[2]);
    CHECK(near(all[1], data.pin_position));
}

TEST_CASE("a ball left in its cup is drawn there, with the prompt in reach") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(start_hub_hole(state, 0));
    state.stroke_count = 3;
    complete_current_hole(state);
    REQUIRE(state.cup_ball.has_value());
    state.player.position = *state.cup_ball;
    refresh_static_anchor_cache(state);
    render_data data = frame_for(state);
    CHECK(data.show_ball);
    CHECK(!data.show_hole);
    CHECK(data.pin_markers.size() == 3U);  // its own pin among them
    CHECK(near(data.ball_position, *state.cup_ball));
    CHECK(data.show_interact_prompt);

    pick_up_cup_ball(state);
    data = frame_for(state);
    CHECK(!data.show_ball);
}

TEST_CASE("every camera rig uses the field of view setting") {
    game_state state = started_hole();
    CHECK(frame_for(state, 75.0f).camera_fov_degrees == 75.0f);
    enter_aiming(state);
    CHECK(live_camera_view(state, 75.0f).fov_degrees == 75.0f);
    update_game(state, action_input(), 0.016f);
    REQUIRE(active_camera_rig(state) == camera_rig::addressing);
    CHECK(live_camera_view(state, 50.0f).fov_degrees == 50.0f);
    update_game(state, action_input(), 0.016f);
    update_game(state, action_input(), 0.016f);
    REQUIRE(active_camera_rig(state) == camera_rig::following_shot);
    CHECK(live_camera_view(state, 90.0f).fov_degrees == 90.0f);
}

TEST_CASE("each game mode has its camera rig") {
    game_state state = started_hole();
    CHECK(active_camera_rig(state) == camera_rig::walking);
    enter_aiming(state);
    CHECK(active_camera_rig(state) == camera_rig::aiming);
    const camera_view aiming = live_camera_view(state, test_fov_degrees);
    CHECK(aiming.position.y > state.ball.position.y);

    update_game(state, action_input(), 0.016f);
    CHECK(active_camera_rig(state) == camera_rig::addressing);
    update_game(state, action_input(), 0.016f);
    update_game(state, action_input(), 0.016f);
    CHECK(active_camera_rig(state) == camera_rig::following_shot);

    // The follow camera stays at the address view of the shot's start.
    const camera_view follow = live_camera_view(state, test_fov_degrees);
    update_game(state, game_input{}, 0.05f);
    CHECK(near(live_camera_view(state, test_fov_degrees).position, follow.position));
}

TEST_CASE("the aim preview starts at the ball and ends on the ground") {
    game_state state = started_hole();
    enter_aiming(state);
    const render_data data = frame_for(state);

    REQUIRE(!data.aim_arc_points.empty());
    CHECK(data.aim_arc_points.size() <= static_cast<std::size_t>(state.tuning.aim_preview.max_points));
    const glm::vec3 last = data.aim_arc_points.back();
    CHECK(last.y >= terrain_height(state.area, last) - 0.001f);
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
    state.online.balls[2] = room_ball{0, glm::vec3(1.0f), 1};
    remote_shot shot;
    shot.result.duration = 1.0f;
    shot.result.trajectory = {glm::vec3(0.0f), glm::vec3(1.0f)};
    shot.elapsed = 2.0f;
    shot.trail = {glm::vec3(0.0f), glm::vec3(1.0f)};
    state.remote_shots[2] = shot;

    render_data data = frame_for(state);
    CHECK(data.remote_avatars.empty());
    CHECK(data.name_tags.empty());
    CHECK(!data.controls.show_group_key);

    state.play = play_mode::online;
    data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    CHECK(data.remote_avatars[0].in_cart);
    CHECK(data.remote_avatars[0].group_id == 4U);
    REQUIRE(data.name_tags.size() == 1U);
    CHECK(data.name_tags[0].name == "OTHER");
    CHECK(data.name_tags[0].position.y > avatar.position.y);
    CHECK(data.name_tags[0].relationship == player_relationship::unknown);
    CHECK(data.remote_balls.size() == 1U);
    REQUIRE(data.remote_trails.size() == 1U);
    CHECK(data.remote_trails[0].alpha < state.tuning.flight_path.alpha);
    CHECK(data.controls.show_group_key);
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

TEST_CASE("another player is drawn emoting, and at their ball addressing it") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    state.play = play_mode::online;
    state.online.account_id = 1;
    state.online.status = net_status::connected;
    state.online.players[2] = room_player{};
    remote_avatar avatar;
    avatar.position = state.player.position + glm::vec3(6.0f, 0.0f, 0.0f);
    trigger_emote(avatar.smoke_emote);
    avatar.smoke_emote.elapsed = 0.5f;
    state.remote_avatars[2] = avatar;

    render_data data = frame_for(state);
    REQUIRE(data.remote_avatars.size() == 1U);
    REQUIRE(data.remote_avatars[0].smoke_elapsed.has_value());
    CHECK(*data.remote_avatars[0].smoke_elapsed == 0.5f);
    CHECK(!data.remote_avatars[0].drink_elapsed.has_value());
    CHECK(!data.remote_avatars[0].swing.has_value());

    // Aiming: turned the way they aim, where they stand.
    const glm::vec3 ball = state.player.position + glm::vec3(0.0f, 0.0f, 4.0f);
    const float aim = 0.5f;
    net_motion aiming;
    aiming.mode = motion_mode::aim;
    aiming.position = avatar.position;
    aiming.yaw = aim;
    state.online.players[2].motion = aiming;
    state.online.players[2].motion_at = 4;
    state.online.balls[2] = room_ball{hub_zone, ball, 0};
    state.remote_avatars[2].yaw = aim;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(near(data.remote_avatars[0].position, avatar.position));
    CHECK(data.remote_avatars[0].yaw == aim);

    // Addressing it: stood at it holding the club, the club down.
    net_motion motion;
    motion.mode = motion_mode::address;
    motion.position = avatar.position;
    motion.yaw = aim;
    state.online.players[2].motion = motion;
    state.online.players[2].motion_at = 5;
    data = frame_for(state);
    REQUIRE(data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].swing->power == 0.0f);
    const glm::vec3 drawn_ball = drawn_ball_center(ball, state.tuning.scale);
    CHECK(near(data.remote_avatars[0].swing->ball_position, drawn_ball));
    CHECK(data.remote_avatars[0].swing->aim_angle == aim);
    const figure_stance stance = figure_address_stance(drawn_ball, state.tuning.scale.ball_visual_radius_meters, aim);
    CHECK(near(data.remote_avatars[0].position, stance.position));
    CHECK(data.remote_avatars[0].yaw == stance.yaw);
    // An arm's length from the ball, not where my camera stands.
    CHECK(horizontal_distance(stance.position, ball) < 1.0f);
    CHECK(near(stance.yaw, yaw_towards(stance.position, ball), 0.3f));

    // Once the shot of that swing is hit, they look where they hit it.
    state.remote_avatars[2].hit_motion_at = 5;
    data = frame_for(state);
    CHECK(!data.remote_avatars[0].swing.has_value());
    CHECK(data.remote_avatars[0].yaw == aim);
}

TEST_CASE("a refusal from the server shows as a notice") {
    game_state state = started_hole();
    state.notice = game_notice{"online.error.too_far", 0.0f};
    CHECK(frame_for(state).notice_label == "TOO FAR AWAY");
}

TEST_CASE("the course map numbers every hole of a hub course at its tee and fits to the holes") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    CHECK(frame_for(state).map_holes.empty());

    state.course_map_active = true;
    const render_data data = frame_for(state);
    REQUIRE(data.map_holes.size() == state.course_holes.size());
    for (std::size_t i = 0; i < data.map_holes.size(); ++i) {
        CHECK(data.map_holes[i].number == std::to_string(i + 1));
        CHECK(near(data.map_holes[i].tee, state.area.signs[i].line_of_play.front()));
        const glm::vec3 tee = data.map_holes[i].tee;
        CHECK((tee.x >= data.course_map_low.x && tee.x <= data.course_map_high.x));
        CHECK((tee.z >= data.course_map_low.z && tee.z <= data.course_map_high.z));
    }
    // The holes, not the land around them.
    CHECK(data.course_map_high.x - data.course_map_low.x <= state.area.extent * 2.0f);
    CHECK(data.course_map_high.z - data.course_map_low.z <= state.area.extent * 2.0f);
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
