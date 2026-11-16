#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/server_errors.h"
#include "game/hub_scene.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace {
constexpr std::uint64_t me = 1;
constexpr std::uint64_t other = 2;

// Online on the fixture course, my bag strapped on my cart with the 7 iron
// in my hand, as my rows have them.
game_state online_with_bag() {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = me;
    state.online.server_now = 1000000000;
    room_player self;
    self.name = "ME";
    self.round_started = true;
    self.round_strokes = std::vector<int>(state.course_holes.size(), 0);
    self.motion = current_motion(state);
    state.online.players[me] = self;
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    const auto iron = std::find_if(state.clubs.begin(), state.clubs.end(), [](const club_definition& club) { return club.label == "7I"; });
    state.online.bags[me] = room_bag{bag_place{bag_where::on_cart, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f}, iron->id};
    update_game(state, game_input{}, 0.02f);
    CHECK(state.bag.where == bag_where::on_cart);
    CHECK(state.clubs[state.selected_club].id == iron->id);
    state.net_commands.clear();
    return state;
}

std::size_t count_commands(const game_state& state, const net_command_type type) {
    return static_cast<std::size_t>(std::count_if(state.net_commands.begin(), state.net_commands.end(),
                                                  [type](const net_command& command) { return command.type == type; }));
}

const std::string& held_club(const game_state& state) {
    return state.clubs[state.selected_club].id;
}

// The fixture's putter (club ids).
std::string putter_of(const game_state& state) {
    const auto putter = std::find_if(state.clubs.begin(), state.clubs.end(),
                                     [](const club_definition& club) { return club.club_class == "putter"; });
    CHECK(putter != state.clubs.end());
    return putter != state.clubs.end() ? putter->id : std::string();
}

void run(game_state& state, const float seconds, const game_input& input = {}) {
    for (float t = 0.0f; t < seconds; t += 0.02f) {
        update_game(state, input, 0.02f);
    }
}

game_input b_key() {
    game_input input;
    input.move_bag = true;
    return input;
}

// The server set my bag down on the ground at `at`.
bag_place set_down_at(const game_state& state, const glm::vec3& at) {
    return bag_place{bag_where::on_ground, cart_seat::driver, 0, anchor_on_terrain(state.area, at), 0.25f};
}

// Standing at `at` on foot.
void stand_at(game_state& state, const glm::vec3& at) {
    state.player.position = anchor_on_terrain(state.area, at);
    state.player.vertical = vertical_body{state.player.position.y, 0.0f, true};
}
}

TEST_CASE("bag sync: my cart towed, my bag lies where the server set it down, in reach there and not at the cart") {
    game_state state = online_with_bag();
    const cart_pose sunk = state.cart.pose;
    const bag_place down = set_down_at(state, step_out_position(sunk, cart_seat::driver, state.tuning.cart));
    // The tow: my bag set down where the cart went in, the cart parked again far off.
    const glm::vec3 parked = sunk.position + glm::vec3(-40.0f, 0.0f, 10.0f);
    state.online.bags[me].place = down;
    state.online.carts[me] = room_cart{anchor_on_terrain(state.area, parked), 0.5f, 0};
    update_game(state, game_input{}, 0.02f);

    CHECK(state.bag.where == bag_where::on_ground);
    CHECK(near(bag_position(state), down.position, 0.0001f));
    CHECK(!state.handling.has_value());
    stand_at(state, bag_spot(*shown_cart(state, 0), cart_seat::driver, state.tuning.cart));
    CHECK(!bag_in_reach(state).has_value());
    stand_at(state, down.position + glm::vec3(0.3f, 0.0f, 0.0f));
    CHECK(bag_in_reach(state).has_value());

    // B there takes it up from the ground, sent once.
    update_game(state, b_key(), 0.02f);
    REQUIRE(count_commands(state, net_command_type::place_bag) == 1U);
    CHECK(state.bag.where == bag_where::on_back);
    REQUIRE(state.handling.has_value());
    CHECK(state.handling->from.where == bag_where::on_ground);
    CHECK(near(state.handling->from.position, down.position, 0.0001f));
}

TEST_CASE("bag sync: the owner of the cart my bag is strapped on leaves, and it is on the ground, reached only there") {
    game_state state = online_with_bag();
    const cart_pose theirs{state.player.position + glm::vec3(20.0f, 0.0f, 0.0f), 0.0f};
    state.online.carts[other] = room_cart{anchor_on_terrain(state.area, theirs.position), 0.0f, 0};
    state.online.bags[me].place = bag_place{bag_where::on_cart, cart_seat::passenger, other, glm::vec3(0.0f), 0.0f};
    update_game(state, game_input{}, 0.02f);
    REQUIRE(state.bag.cart_owner == other);

    // They leave the room: their cart goes, my bag is set down by its door.
    const bag_place down = set_down_at(state, step_out_position(*shown_cart(state, other), cart_seat::passenger, state.tuning.cart));
    state.online.carts.erase(other);
    state.online.bags[me].place = down;
    update_game(state, game_input{}, 0.02f);
    CHECK(state.bag.where == bag_where::on_ground);
    CHECK(near(bag_position(state), down.position, 0.0001f));
    stand_at(state, down.position + glm::vec3(30.0f, 0.0f, 0.0f));
    CHECK(!bag_in_reach(state).has_value());
    update_game(state, b_key(), 0.02f);
    CHECK(count_commands(state, net_command_type::place_bag) == 0U);
    stand_at(state, down.position + glm::vec3(0.3f, 0.0f, 0.0f));
    CHECK(bag_in_reach(state).has_value());
}

TEST_CASE("bag sync: an accepted bag move arriving late changes nothing, its handling playing on uncut and unrepeated") {
    game_state state = online_with_bag();
    stand_at(state, bag_position(state));
    update_game(state, b_key(), 0.02f);
    REQUIRE(state.handling.has_value());
    REQUIRE(count_commands(state, net_command_type::place_bag) == 1U);
    const bag_place sent = state.net_commands.back().bag;
    CHECK(bag_on_back(state));

    // Half its handling with the row still as it was: my move stands.
    run(state, handling_seconds(*state.handling, state.tuning.player) * 0.5f);
    REQUIRE(state.handling.has_value());
    CHECK(bag_on_back(state));
    const float elapsed = state.handling->elapsed;
    state.online.bags[me].place = sent;
    update_game(state, game_input{}, 0.02f);
    REQUIRE(state.handling.has_value());
    CHECK(state.handling->elapsed > elapsed);
    CHECK(state.handling->to.where == bag_where::on_back);
    CHECK(bag_on_back(state));
    CHECK(!state.bag_expected.has_value());

    // Later, a second move shown only after its handling has played.
    run(state, 1.0f);
    CHECK(!state.handling.has_value());
    update_game(state, b_key(), 0.02f);
    REQUIRE(count_commands(state, net_command_type::place_bag) == 2U);
    const bag_place second = state.net_commands.back().bag;
    run(state, handling_seconds(*state.handling, state.tuning.player) + 1.0f);
    CHECK(!state.handling.has_value());
    CHECK(same_place(state.bag, second));
    state.online.bags[me].place = second;
    update_game(state, game_input{}, 0.02f);
    CHECK(!state.handling.has_value());
    CHECK(same_place(state.bag, second));
    CHECK(count_commands(state, net_command_type::place_bag) == 2U);
    CHECK(state.motion_sync.breaches == 0U);
}

TEST_CASE("bag sync: a refused move puts back the whole row, and the next row change is taken") {
    game_state state = online_with_bag();
    stand_at(state, bag_position(state));
    update_game(state, b_key(), 0.02f);
    REQUIRE(bag_on_back(state));
    state.online.refusals.push_back(reducer_failure{reducer_place_bag, error_bag_out_of_reach});
    update_game(state, game_input{}, 0.02f);
    CHECK(state.bag.where == bag_where::on_cart);
    CHECK(!state.handling.has_value());
    CHECK(!state.bag_expected.has_value());
    CHECK(bag_in_reach(state).has_value());

    const bag_place down = set_down_at(state, state.player.position + glm::vec3(2.0f, 0.0f, 0.0f));
    state.online.bags[me].place = down;
    update_game(state, game_input{}, 0.02f);
    CHECK(near(bag_position(state), down.position, 0.0001f));
}

TEST_CASE("bag sync: a row change I did not ask for is taken around what I wait for, and others' rows leave mine be") {
    game_state state = online_with_bag();
    const std::string iron = held_club(state);
    const std::string putter = putter_of(state);

    // My bag move waiting, its handling playing: the server hands me my putter.
    stand_at(state, bag_position(state));
    update_game(state, b_key(), 0.02f);
    REQUIRE(state.handling.has_value());
    const float elapsed = state.handling->elapsed;
    state.online.bags[me].club_in_hand = putter;
    state.online.bags[other] = room_bag{bag_place{}, iron};
    update_game(state, game_input{}, 0.02f);
    CHECK(held_club(state) == putter);
    CHECK(bag_on_back(state));
    REQUIRE(state.handling.has_value());
    CHECK(state.handling->kind == handling_kind::bag);
    CHECK(state.handling->elapsed > elapsed);
    state.online.bags[other].place = set_down_at(state, state.player.position + glm::vec3(5.0f, 0.0f, 0.0f));
    update_game(state, game_input{}, 0.02f);
    CHECK(bag_on_back(state));
    CHECK(held_club(state) == putter);
    state.online.bags[me].place = bag_place{};
    update_game(state, game_input{}, 0.02f);
    CHECK(!state.bag_expected.has_value());
    run(state, 1.0f);

    // A club taken, waiting: the server sets my bag down; it lies there,
    // and the club I took stays in my hand until the row shows it.
    state.bag = bag_place{bag_where::on_cart, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f};
    state.online.bags[me].place = state.bag;
    update_game(state, game_input{}, 0.02f);
    stand_at(state, bag_position(state));
    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.picker.has_value());
    game_input longer;
    longer.picker_steps = 1;
    update_game(state, longer, 0.02f);
    update_game(state, interact_input(), 0.02f);
    REQUIRE(count_commands(state, net_command_type::take_club) == 1U);
    const std::string taken = held_club(state);
    REQUIRE(taken != putter);
    const bag_place down = set_down_at(state, state.player.position + glm::vec3(1.0f, 0.0f, 1.0f));
    state.online.bags[me].place = down;
    update_game(state, game_input{}, 0.02f);
    CHECK(near(bag_position(state), down.position, 0.0001f));
    CHECK(held_club(state) == taken);
    REQUIRE(state.handling.has_value());
    CHECK(state.handling->kind == handling_kind::club);
    state.online.bags[me].club_in_hand = taken;
    update_game(state, game_input{}, 0.02f);
    CHECK(!state.bag_expected.has_value());
    CHECK(held_club(state) == taken);
    CHECK(count_commands(state, net_command_type::take_club) == 1U);
    CHECK(count_commands(state, net_command_type::place_bag) == 1U);
}

TEST_CASE("bag sync: a move the row never shows nor refuses gives way to the row") {
    game_state state = online_with_bag();
    stand_at(state, bag_position(state));
    update_game(state, b_key(), 0.02f);
    REQUIRE(bag_on_back(state));
    run(state, state.tuning.net.max_unacked_seconds - 0.2f);
    CHECK(bag_on_back(state));
    run(state, 0.4f);
    CHECK(state.bag.where == bag_where::on_cart);
    CHECK(!state.bag_expected.has_value());
    CHECK(state.motion_sync.breaches == 1U);
    CHECK(count_commands(state, net_command_type::place_bag) == 1U);
}

TEST_CASE("bag sync: a course warmup hands me my putter with my ball, sending no club of my own") {
    game_state state = online_with_bag();
    const std::string putter = putter_of(state);
    REQUIRE(held_club(state) != putter);
    state.online.players[me].round_started = false;
    state.online.room_ready = true;
    request_ball_preparation(state, net_command_type::tee_up, 0, true);
    REQUIRE(state.hub_flow.ball_request.has_value());
    state.net_commands.clear();

    // The server's answer, in one transaction: my warmup ball, me at it, my putter.
    const glm::vec3 station = warmup_position(state.area, 0, state.tuning);
    const glm::vec3 ball = resting_ball_position(state.area, station, state.tuning.scale.ball_physics_radius_meters);
    state.online.balls[me] = room_ball{0, ball, 0, false, ball_kind::warmup};
    ++state.online.ball_updates;
    state.online.players[me].motion->position = tee_stance_position(state.area, ball, state.area.cups[0].centre,
                                                                    state.tuning.player.ball_stand_off_distance);
    state.online.bags[me].club_in_hand = putter;
    update_game(state, game_input{}, 0.02f);
    REQUIRE(!state.hub_flow.ball_request.has_value());
    CHECK(held_club(state) == putter);
    CHECK(!state.handling.has_value());
    CHECK(count_commands(state, net_command_type::take_club) == 0U);

    // My cart towed meanwhile: my bag goes to the ground, my putter stays.
    const bag_place down = set_down_at(state, state.player.position + glm::vec3(2.0f, 0.0f, 0.0f));
    state.online.bags[me].place = down;
    update_game(state, game_input{}, 0.02f);
    CHECK(near(bag_position(state), down.position, 0.0001f));
    CHECK(held_club(state) == putter);
    CHECK(count_commands(state, net_command_type::take_club) == 0U);
    CHECK(count_commands(state, net_command_type::place_bag) == 0U);
}
