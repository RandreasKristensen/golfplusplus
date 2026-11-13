#include "doctest.h"

#include "core/render_frame.h"
#include "core/render_gear.h"
#include "game/golf_bag.h"
#include "renderer/bag_batch.h"

#include "test_support.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace {
glm::vec3 point_in(const glm::mat4& frame, const prop_model& model, const char* point) {
    return glm::vec3(frame * glm::vec4(model.point(point), 1.0f));
}
}

TEST_CASE("handling a club keeps walking and turning without opening another picker") {
    game_state state = started_hole();
    state.mode = game_mode::walking;
    state.player.position = bag_position(state) - yaw_direction(state.cart.pose.yaw) * 0.5f;
    open_club_picker(state);
    game_input choose;
    choose.picker_steps = -1;
    update_game(state, choose, 0.02f);
    const glm::vec3 before = state.player.position;
    game_input moving;
    moving.forward_held = true;
    moving.look_yaw = 0.1f;
    moving.interact = true;
    update_game(state, moving, 0.02f);
    REQUIRE(state.handling.has_value());
    REQUIRE(state.handling->kind == handling_kind::club);
    CHECK(horizontal_distance(state.player.position, before) > 0.001f);
    const float yaw = state.player.yaw;
    const glm::vec3 swapping = state.player.position;
    update_game(state, moving, 0.02f);
    CHECK(state.player.yaw != yaw);
    CHECK(horizontal_distance(state.player.position, swapping) > 0.001f);
    CHECK(!state.picker.has_value());
}

TEST_CASE("a handled club is inserted grip first and withdrawn by the head before turning in the picture") {
    game_state state = started_game(fixture_hub_course());
    const prop_catalog& props = test_props();
    const prop_model& bag = props.at("golf_bag");
    render_remote_avatar golfer;
    golfer.position = glm::vec3(0.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.8f, -5.0f), glm::vec3(0.0f, 1.0f, 1.0f), world_up);
    const glm::mat4 projection = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.05f, 100.0f);
    const auto screen = [&](const glm::vec3& world) {
        const glm::vec4 clip = projection * view * glm::vec4(world, 1.0f);
        CHECK(clip.w > 0.0f);
        const glm::vec2 pixel = glm::vec2(clip) / clip.w;
        CHECK(std::abs(pixel.x) < 1.0f);
        CHECK(std::abs(pixel.y) < 1.0f);
        return pixel;
    };
    for (const char* model : {"club_iron", "club_wood", "club_putter"}) {
        const prop_model& club = props.at(model);
        const glm::mat4 bag_frame = ground_bag_frame(render_ground_bag{"golf_bag", glm::vec3(0.0f, 0.0f, 0.7f), 0.0f, {}}, bag);
        const glm::vec3 mouth = point_in(bag_frame, bag, "mouth");
        const glm::vec3 bottom = point_in(bag_frame, bag, "hang");
        const auto pose = [&](const float progress, const bool drawing) {
            return handling_club_pose(golfer, state.tuning.camera.walking_eye_height, bag_frame, bag, club, progress, drawing);
        };
        const handled_club_pose carried = pose(0.0f, false);
        const handled_club_pose clear = pose(0.5f, false);
        const handled_club_pose seated = pose(1.0f, false);
        const glm::vec3 head = point_in(seated.frame, club, "face");
        const glm::vec3 grip = point_in(seated.frame, club, "grip");
        CHECK(screen(head).y > screen(mouth).y);
        CHECK(screen(mouth).y > screen(grip).y);
        CHECK(screen(grip).y > screen(bottom).y);
        CHECK(screen(point_in(clear.frame, club, "grip")).y > screen(mouth).y);
        CHECK(screen(point_in(clear.frame, club, "face")).y > screen(head).y);
        CHECK(screen(point_in(carried.frame, club, "face")).y < screen(point_in(carried.frame, club, "grip")).y);
        CHECK(near(seated.hand, head, 0.0001f));
        const handled_club_pose lifted = pose(0.5f, true);
        CHECK(near(lifted.hand, point_in(lifted.frame, club, "face"), 0.0001f));
        CHECK(screen(lifted.hand).y > screen(pose(0.0f, true).hand).y);
        const handled_club_pose drawn = pose(1.0f, true);
        CHECK(near(drawn.hand, point_in(drawn.frame, club, "grip"), 0.0001f));
        CHECK(near(point_in(drawn.frame, club, "face"), point_in(carried.frame, club, "face"), 0.0001f));
    }
}

TEST_CASE("remote handling follows walking without restarting and uses the local club motion") {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = 1;
    state.online.server_now = 1000000000;
    room_player player;
    player.motion = net_motion{};
    player.motion->position = state.player.position + glm::vec3(5.0f, 0.0f, 0.0f);
    player.motion_at = state.online.server_now;
    state.online.players[2] = player;
    state.online.loadouts[2] = state.default_loadout;
    const bag_place down{bag_where::on_ground, cart_seat::driver, 0, player.motion->position, 0.0f};
    state.online.bags[2] = room_bag{down, "driver"};
    update_remote_players(state, 0.02f);
    state.online.bags[2].club_in_hand = "putter";
    update_remote_players(state, 0.02f);
    REQUIRE(state.remote_avatars.at(2).handling.has_value());
    update_remote_players(state, state.tuning.player.club_swap_seconds * 0.7f);
    const render_data remote = make_render_data(state, shipped_text_assets(), shipped_content().skills,
                                                 render_meshes{}, test_props(), 60.0f, false);
    REQUIRE(remote.remote_avatars.size() == 1U);
    REQUIRE(remote.handled_props.size() == 1U);
    CHECK(remote.remote_avatars.front().carried_club_model.empty());
    render_data local;
    render_remote_avatar figure;
    figure.position = state.remote_avatars.at(2).position;
    figure.yaw = state.remote_avatars.at(2).yaw;
    add_handling(local, figure, state, *state.remote_avatars.at(2).handling, 0, "golf_bag", test_props());
    REQUIRE(local.handled_props.size() == 1U);
    const prop_model& club = test_props().at("club_putter");
    CHECK(near(point_in(handled_prop_frame(local.handled_props.front()), club, "face"),
               point_in(handled_prop_frame(remote.handled_props.front()), club, "face"), 0.0001f));
    REQUIRE(figure.reach.has_value());
    REQUIRE(remote.remote_avatars.front().reach.has_value());
    CHECK(near(*figure.reach, *remote.remote_avatars.front().reach, 0.0001f));

    state.online.bags[2].place = bag_place{};
    update_remote_players(state, 0.02f);
    REQUIRE(state.remote_avatars.at(2).handling.has_value());
    state.online.players[2].motion->position += glm::vec3(1.0f, 0.0f, 1.0f);
    state.online.players[2].motion->yaw = 0.5f;
    state.online.players[2].motion_at += 100000;
    state.online.bags[2].place.position = state.online.players[2].motion->position;
    state.online.bags[2].place.yaw = 0.5f;
    update_remote_players(state, 0.1f);
    REQUIRE(state.remote_avatars.at(2).handling.has_value());
    CHECK(state.remote_avatars.at(2).handling->elapsed > 0.09f);
    CHECK(state.remote_avatars.at(2).yaw == 0.5f);
}
