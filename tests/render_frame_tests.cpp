#include "doctest.h"

#include "core/render_frame.h"
#include "game/course_session.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"

#include "test_support.h"

namespace {
render_data frame_for(const game_state& state) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, input_state{}, text, shipped_content().skills, render_meshes{});
}
}

TEST_CASE("a hole frame shows the ball, tee and pin but no hub markers") {
    game_state state = started_hole();
    refresh_static_anchor_cache(state);
    const render_data data = frame_for(state);

    CHECK(data.show_ball);
    CHECK(data.show_hole);
    CHECK(data.pin_position == state.static_anchors.pin_anchor);
    CHECK(data.hub_tee_markers == nullptr);
    CHECK(data.start_markers.empty());
    CHECK(data.show_interact_prompt);
    CHECK(data.camera_fov_degrees == state.tuning.camera.fov_degrees);
}

TEST_CASE("a hub frame hides the ball and shows unplayed starts and claimable collectibles") {
    game_state state = started_game(fixture_hub_course());
    refresh_static_anchor_cache(state);
    render_data data = frame_for(state);
    CHECK(!data.show_ball);
    CHECK(!data.show_hole);
    CHECK(data.start_markers.size() == 3U);
    // The fitness cache needs fitness level 2, so two of three are claimable.
    CHECK(data.collectible_markers.size() == 2U);

    REQUIRE(start_hub_hole(state, 0));
    state.stroke_count = 3;
    complete_current_hole(state);
    refresh_static_anchor_cache(state);
    data = frame_for(state);
    CHECK(data.start_markers.size() == 2U);
}

TEST_CASE("each game mode has its camera rig") {
    game_state state = started_hole();
    CHECK(active_camera_rig(state) == camera_rig::walking);
    enter_aiming(state);
    CHECK(active_camera_rig(state) == camera_rig::aiming);
    const camera_view aiming = live_camera_view(state);
    CHECK(aiming.position.y > state.ball.position.y);

    update_game(state, action_input(), 0.016f);
    CHECK(active_camera_rig(state) == camera_rig::addressing);
    update_game(state, action_input(), 0.016f);
    update_game(state, action_input(), 0.016f);
    CHECK(active_camera_rig(state) == camera_rig::following_shot);

    // The follow camera stays at the address view of the shot's start.
    const camera_view follow = live_camera_view(state);
    update_game(state, game_input{}, 0.05f);
    CHECK(near(live_camera_view(state).position, follow.position));
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
    CHECK(data.remote_balls.size() == 1U);
    REQUIRE(data.remote_trails.size() == 1U);
    CHECK(data.remote_trails[0].alpha < state.tuning.flight_path.alpha);
    CHECK(data.controls.show_group_key);
    CHECK(data.avatar_eye_height == state.tuning.camera.walking_eye_height);
}

TEST_CASE("a refusal from the server shows as a notice") {
    game_state state = started_hole();
    state.notice = game_notice{"online.error.too_far", 0.0f};
    CHECK(frame_for(state).notice_label == "TOO FAR AWAY");
}
