#include "doctest.h"

#include "game/address_play.h"
#include "game/course_session.h"
#include "game/hub_session.h"
#include "game/hub_ball.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/interactables.h"
#include "game/save_data.h"
#include "game/content_files.h"
#include "game/json_util.h"
#include "game/scorecard.h"
#include "game/online_play.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cmath>
#include <glm/trigonometric.hpp>

namespace {
// The fixture course played as a practice ground with no carts.
game_state practice_game() {
    auto state = started_game(fixture_hub_course());
    state.kind = place_kind::hub; state.place.carts = false;
    hub_definition hub;
    for (std::size_t i = 0; i < state.holes.size(); ++i) {
        hub.tee_mats.push_back({state.world.hole_starts[i].position, state.holes[i].pin_position,
            state.world.hole_starts[i].interaction_radius});
    }
    state.hub = hub;
    state.round.reset(); state.holes.clear(); state.course_holes.clear();
    state.area.signs.clear(); state.area.way_signs.clear();
    park_cart(state); strap_bag_on_cart(state);
    return state;
}

game_input bag_input() {
    game_input input;
    input.move_bag = true;
    return input;
}

// F, then looking down at the ball the swing, played until it rests.
void hit(game_state& state) {
    state.address.look = down_look(state.tuning.camera);
    start_swing_on_time(state);
    swing_through(state);
    for (int frame = 0; frame < 2000 && shot_playing(state); ++frame) {
        update_game(state, game_input{}, 0.05f);
    }
}
}

TEST_CASE("a practice ground without carts starts with my bag on my back, and no cart of mine anywhere") {
    game_state state = practice_game();
    CHECK(!own_cart(state));
    CHECK(bag_on_back(state));
    CHECK(carts_in_world(state).empty());
    CHECK(!seat_door_at(state));
    // B sets it down at my feet, where its picker is in reach.
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    CHECK(!bag_on_back(state));
    CHECK(bag_in_reach(state).has_value());
}

TEST_CASE("practising, F after a shot drops a fresh ball where I stand, set up as the last, with nothing scored or earned") {
    game_state state = practice_game();
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    const glm::vec3 tee = state.ball->position;
    // A setup of my own: turned, opened, a shorter swing.
    state.address.setup.aim_angle = wrap_angle(state.address.setup.aim_angle + glm::radians(3.0f));
    state.address.setup.face_open = glm::radians(2.0f);
    state.address.setup.backswing = 0.6f;
    const std::string progress_before = save_data_to_json(state.save);

    hit(state);
    REQUIRE(state.ball->strokes == 1);
    CHECK(horizontal_distance(state.ball->position, tee) > 1.0f);
    // Where the swing left me, the station is the nearest thing F acts on.
    const glm::vec3 stood = state.player.position;
    CHECK(horizontal_distance(stood, tee) < state.tuning.player.ball_interact_radius);
    const std::optional<interactable> nearest = nearest_interactable(state);
    REQUIRE(nearest.has_value());
    CHECK(nearest->kind == interactable_kind::tee);
    CHECK(save_data_to_json(state.save) == progress_before);
    CHECK(!state.round);

    const shot_setup last = state.address.setup;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(state.ball->strokes == 0);
    CHECK(near(state.ball->position, tee));
    CHECK(near(state.address.setup.aim_angle, last.aim_angle, 0.0001f));
    CHECK(near(state.address.setup.face_open, last.face_open, 0.0001f));
    CHECK(near(state.address.setup.backswing, last.backswing, 0.0001f));
    CHECK(looking_down(state.address.look, state.tuning.camera));
    // I stand where I stood: nothing to walk.
    CHECK(horizontal_distance(state.player.position, stood) < 0.01f);
}

TEST_CASE("practising, another station takes my ball at once, no give-up question, and a holed ball needs no picking up") {
    game_state state = practice_game();
    update_game(state, bag_input(), 0.016f);
    finish_handling(state);
    update_game(state, interact_input(), 0.016f);
    hit(state);
    REQUIRE(ball_in_play(state));

    state.player.position = state.world.hole_starts[1].position;
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.give_up_prompt);
    REQUIRE(state.ball.has_value());
    CHECK(!state.ball->hole);
    CHECK(state.ball->strokes == 0);

    // Holed: it lies in the cup, nothing is completed, and F drops another.
    state.mode = game_mode::walking;
    const glm::vec3 cup = state.area.cups[1].centre;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball->position, cup}, {}});
    for (int i = 0; i < 20 && ball_in_play(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    REQUIRE(state.ball->place == ball_place::in_cup);
    CHECK(!state.round);
    state.player.position = state.world.hole_starts[1].position;
    update_game(state, interact_input(), 0.016f);
    CHECK(state.ball->place == ball_place::teed);
}

TEST_CASE("practising, Tab shows no scorecard") {
    game_state state = practice_game();
    game_input tab;
    tab.scorecard_held = true;
    update_game(state, tab, 0.016f);
    CHECK(!state.scorecard_active);
}

TEST_CASE("hub manifest requires facilities and forbids course holes") {
    const auto source = read_text_file(fixture_root() + "/world/hub/hub.json");
    REQUIRE(source);
    const auto parsed = parse_json(*source);
    REQUIRE(parsed);
    REQUIRE(parse_hub_from_text(*source));
    for (const char* key : {"tee_mats", "targets", "practice_cups", "arrival", "hub", "world"}) {
        auto missing = *parsed; missing.erase(key);
        CHECK(!parse_hub_from_text(missing.dump()));
    }
    auto holes = *parsed; holes["holes"] = json::array();
    CHECK(!parse_hub_from_text(holes.dump()));
}

TEST_CASE("entry and return adopt the same hole-free hub in either play mode") {
    for (const bool online : {false, true}) {
        auto state = make_game_state(fixture_content(), save_data{});
        state.play = online ? play_mode::online : play_mode::offline;
        state.online.room = online_room{7, online_hub_course, 1, 1, place_kind::hub};
        state.online.room_ready = true;
        REQUIRE(enter_place(state, online_hub_course));
        const auto arrival = state.player.position;
        REQUIRE(tee_up_hub(state, 0) || online);
        if (!online) { REQUIRE(state.ball); CHECK(!state.ball->hole); CHECK(state.ball->kind == ball_kind::practice); }
        REQUIRE(enter_place(state, online_hub_course));
        CHECK(state.player.position == arrival);
        CHECK(!state.ball); CHECK(!state.round); CHECK(state.holes.empty());
        CHECK(build_scorecard_data(state, shipped_text_assets().strings).pars.empty());
        CHECK(state.area.signs.empty()); CHECK(state.area.way_signs.empty());
        const auto before = save_data_to_json(state.save);
        give_up_hole(state); complete_current_hole(state); start_next_round(state);
        CHECK(!state.round); CHECK(save_data_to_json(state.save) == before);
    }
}

TEST_CASE("practice ball drops on each walkable lie and pickup preserves the player and progress") {
    for (const auto material : {material_zone_type::fairway, material_zone_type::green, material_zone_type::bunker, material_zone_type::unknown}) {
        auto state = practice_game();
        auto hole = straight_hole({-10, 0, 0}, {10, 0, 0}, 30);
        hole.ribbon_fairway = false;
        if (material != material_zone_type::unknown) {
            hole.material_zones.push_back(make_material_zone(material, {{-5,-5},{5,-5},{5,5},{-5,5}}));
        }
        state.area = hole_area(hole, state.tuning);
        state.player.position = anchor_on_terrain(state.area, {0,0,0});
        const auto player = state.player.position; const auto bag = state.bag; const auto revision = state.terrain_render_revision;
        const auto progress = save_data_to_json(state.save);
        request_practice_drop(state);
        REQUIRE(state.ball); CHECK(!state.ball->hole); CHECK(state.ball->kind == ball_kind::practice);
        CHECK(near(state.ball->position.y, terrain_height(state.area, player) + state.tuning.scale.ball_physics_radius_meters));
        const auto ball = state.ball->position;
        request_practice_drop(state); CHECK(state.ball->position == ball);
        state.player.position += glm::vec3(10,0,0); CHECK(!pick_up_practice_ball(state));
        state.player.position = player; REQUIRE(pick_up_practice_ball(state));
        CHECK(!state.ball); CHECK(state.player.position == player); CHECK(state.bag.where == bag.where);
        CHECK(state.terrain_render_revision == revision); CHECK(save_data_to_json(state.save) == progress);
    }
    auto course = started_game(fixture_hub_course()); request_practice_drop(course); CHECK(!course.ball);
}

TEST_CASE("online practice drop and pickup wait for whole rows and restore them on refusal") {
    auto state = practice_game(); state.play = play_mode::online;
    state.online.account_id = 1; state.online.room_ready = true;
    state.online.room = online_room{7, online_hub_course, 1, 1, place_kind::hub};
    state.online.players[1].motion = current_motion(state);
    request_practice_drop(state); CHECK(!state.ball); REQUIRE(state.hub_flow.ball_request);
    const auto position = resting_ball_position(state.area, state.player.position, state.tuning.scale.ball_physics_radius_meters);
    state.online.balls[1] = {-1, position, 0, false, ball_kind::practice}; ++state.online.ball_updates;
    update_online_play(state, 0); REQUIRE(state.ball); CHECK(!state.ball->hole); CHECK(!state.hub_flow.ball_request);
    const auto pose = state.player.position;
    REQUIRE(pick_up_practice_ball(state)); REQUIRE(state.ball);
    state.online.refusals.push_back({reducer_pick_up_ball, error_too_far});
    update_online_play(state, 0); REQUIRE(state.ball); CHECK(state.ball->position == position);
    REQUIRE(pick_up_practice_ball(state)); state.online.balls.clear(); ++state.online.ball_updates;
    update_online_play(state, 0); CHECK(!state.ball); CHECK(!state.hub_flow.ball_request); CHECK(state.player.position == pose);

    // A drop over my ball waits for the fresh row, not my old ball's.
    state.online.balls[1] = {-1, position + glm::vec3(80, 0, 0), 2, false, ball_kind::practice};
    state.ball = my_ball{std::nullopt, position + glm::vec3(80, 0, 0), 2, ball_place::at_rest, ball_kind::practice};
    request_practice_drop(state); REQUIRE(state.hub_flow.ball_request);
    ++state.online.ball_updates;
    update_online_play(state, 0); CHECK(state.hub_flow.ball_request);
    state.online.balls[1] = {-1, position, 0, false, ball_kind::practice}; ++state.online.ball_updates;
    update_online_play(state, 0); CHECK(!state.hub_flow.ball_request);
    REQUIRE(state.ball); CHECK(near(state.ball->position, position, 0.001f));
}

TEST_CASE("another golfer's hub practice shot flies from its event before resting") {
    auto state = practice_game();
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = 1;
    REQUIRE(state.holes.empty());
    shot_setup setup;
    setup.ball_start = resting_ball_position(state.area, state.player.position, state.tuning.scale.ball_physics_radius_meters);
    setup.club_id = state.clubs.front().id;
    setup = stocked(setup, state, 0.6f);
    auto shot = played_shot(2, -1, 1, setup, {}, false);
    shot.rest = room_shot_result(state, shot).rest_position;
    state.online.balls[2] = {-1, shot.rest, 1, false, ball_kind::practice};
    state.online.shots.push_back(shot);
    update_online_play(state, 0);
    REQUIRE(state.remote_shots.count(2) == 1);
    CHECK(state.online.shots.empty());
    const auto& playing = state.remote_shots.at(2);
    CHECK(remote_shot_playing(playing));
    REQUIRE(remote_balls(state).size() == 1);
    CHECK(near(remote_balls(state).front().position, setup.ball_start));
    const float before = playing.elapsed;
    update_remote_players(state, 0.2f);
    CHECK(playing.elapsed > before);
    update_remote_players(state, playing.result.duration - playing.elapsed);
    REQUIRE(remote_balls(state).size() == 1);
    CHECK(near(remote_balls(state).front().position, shot.rest));

    state.remote_shots.clear();
    shot.hole = 0;  // A scored-course event cannot start a hub replay.
    start_remote_shot(state, shot);
    CHECK(state.remote_shots.empty());
    state.kind = place_kind::course;
    shot.hole = -1;
    start_remote_shot(state, shot);
    CHECK(state.remote_shots.empty());
}

TEST_CASE("a practice drop replaces my ball wherever it lies or flies, whatever I am doing") {
    auto state = practice_game();
    const auto at = resting_ball_position(state.area, state.player.position, state.tuning.scale.ball_physics_radius_meters);
    state.ball = my_ball{std::nullopt, at + glm::vec3(150, 0, 0), 3, ball_place::at_rest, ball_kind::practice, std::size_t{0}};
    request_practice_drop(state);
    REQUIRE(state.ball); CHECK(near(state.ball->position, at, 0.001f)); CHECK(state.ball->strokes == 0);
    CHECK(!state.ball->tee_mat); CHECK(!state.notice);
    state.ball->place = ball_place::in_cup; state.ball->position = at + glm::vec3(0, 0, 40);
    request_practice_drop(state);
    REQUIRE(state.ball); CHECK(state.ball->place == ball_place::at_rest); CHECK(near(state.ball->position, at, 0.001f));
    // Mid-shot, addressing or in the air: a fresh ball at my feet, and I walk.
    play_shot(state, shot_result{at + glm::vec3(50, 0, 0), false, 3.0f, {at, at + glm::vec3(50, 0, 0)}, {}});
    state.player.vertical.grounded = false;
    game_input drop; drop.drop_practice_ball = true;
    update_game(state, drop, 0.016f);
    CHECK(!shot_playing(state)); CHECK(state.mode == game_mode::walking);
    REQUIRE(state.ball); CHECK(state.ball->place == ball_place::at_rest); CHECK(state.ball->strokes == 0);
    state.mode = game_mode::addressing;
    update_game(state, drop, 0.016f);
    CHECK(state.mode == game_mode::walking); CHECK(!state.notice);
}

TEST_CASE("a practice drop rests on whatever is under it, anywhere in the hub, never on a course") {
    auto state = practice_game();
    const auto at = state.player.position;
    const float radius = state.tuning.scale.ball_physics_radius_meters;
    CHECK(!practice_drop_position(state.area, place_kind::course, at, at, state.tuning));
    // A request beyond reach of where the server has me drops at my feet instead.
    const auto far = practice_drop_position(state.area, place_kind::hub, at, at + glm::vec3(100, 0, 0), state.tuning);
    REQUIRE(far); CHECK(near(*far, resting_ball_position(state.area, at, radius), 0.001f));
    const auto nan = practice_drop_position(state.area, place_kind::hub, at, glm::vec3(std::nanf("")), state.tuning);
    REQUIRE(nan); CHECK(near(*nan, resting_ball_position(state.area, at, radius), 0.001f));
    auto hole = straight_hole({-10,0,0}, {10,0,0}, 30);
    hole.material_zones.push_back(make_material_zone(material_zone_type::water, {{-5,-5},{5,-5},{5,5},{-5,5}}));
    const auto area = hole_area(hole, state.tuning);
    const auto wet = practice_drop_position(area, place_kind::hub, {0,0,0}, {0,0,0}, state.tuning);
    REQUIRE(wet); CHECK(near(*wet, resting_ball_position(area, {0,0,0}, radius), 0.001f));
}
