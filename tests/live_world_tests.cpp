#include "doctest.h"

#include "core/play_screen.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/group_round.h"
#include "game/interactables.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/remote_players.h"
#include "game/round_state.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace {
constexpr std::uint64_t me = 1;
constexpr std::uint64_t other = 2;

// Online on the fixture course, connected, with my own room rows.
game_state online_course() {
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
    self.motion_at = state.online.server_now;
    state.online.players[me] = self;
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    return state;
}

void add_other(game_state& state, const glm::vec3& offset, const std::uint64_t group_id) {
    room_player player;
    player.name = "OTHER";
    player.group_id = group_id;
    net_motion motion;
    motion.position = state.player.position + offset;
    player.motion = motion;
    player.motion_at = state.online.server_now;
    state.online.players[other] = player;
}

std::size_t count_commands(const game_state& state, const net_command_type type) {
    return static_cast<std::size_t>(std::count_if(state.net_commands.begin(), state.net_commands.end(),
                                                  [type](const net_command& command) { return command.type == type; }));
}

void run(game_state& state, const float seconds, const game_input& input = {}) {
    for (float t = 0.0f; t < seconds; t += 0.02f) {
        update_game(state, input, 0.02f);
    }
}

// What app does with a frame of play (core/play_screen.h): the round takes
// the player's input; under another screen the world updates, if at all, with none.
void app_frame(game_state& state, const bool confirm_active, const game_input& input = {}) {
    const play_frame frame = play_frame_for(state, confirm_active, false);
    if (frame.screen == play_screen::round) {
        update_game(state, input, 0.02f);
    } else if (frame.world_updates) {
        update_game(state, game_input{}, 0.02f);
    }
}

// `account`'s shot from the server: struck from hole 1's tee.
void shoots(game_state& state, const std::uint64_t account) {
    const course_hole& marker = state.holes[0];
    shot_setup setup;
    setup.ball_start = resting_ball_position(state.area, marker.tee_position, state.tuning.scale.ball_physics_radius_meters);
    setup.aim_angle = yaw_towards(marker.tee_position, marker.pin_position);
    setup.club_id = state.clubs.front().id;
    setup = stocked(setup, state, 0.8f);
    const glm::vec3 rest = setup.ball_start + glm::vec3(3.0f, 0.0f, 3.0f);
    state.online.balls[account] = room_ball{0, rest, 1};
    state.online.shots.push_back(played_shot(account, 0, 1, setup, rest, false));
}

// My shot on hole 1, struck and answered by the server, the camera following it.
void my_shot_playing(game_state& state) {
    tee_up(state, 0, false);
    state.online.balls[me] = room_ball{0, state.ball->position, 0, false};
    state.online.players[me].motion = current_motion(state);
    update_game(state, game_input{}, 0.0f);
    hit_selected_club(state);
    REQUIRE(shot_playing(state));
    state.online.balls[me] = room_ball{0, state.shot->result.rest_position, 1, false};
}

// Grouped with the other player, every hole holed, the last while they still
// play it: my results wait for them.
void waiting_on_group(game_state& state) {
    state.online.players[me].group_id = 3;
    add_other(state, glm::vec3(30.0f, 0.0f, 0.0f), 3);
    const std::size_t last = state.course_holes.size() - 1U;
    state.online.balls[other] = room_ball{static_cast<int>(last), state.holes[last].pin_position, 3, false};
    for (std::size_t i = 0; i <= last; ++i) {
        pick_up_from_cup(state);
        tee_up(state, i, false);
        state.ball->strokes = 3;
        complete_current_hole(state);
    }
    REQUIRE(waiting_for_group(state));
}

// Sat in the other's passenger seat, confirmed, while they drive it.
void riding_with_other(game_state& state, const cart_pose& parked) {
    state.online.carts[other] = room_cart{parked.position, parked.yaw, me, cart_condition::upright, other};
    state.ride = cart_ride{other, cart_seat::passenger, std::nullopt, true};
}

// Their latest ride motion (game/ride_sync.h), the cart `forward` along +Z
// from where it was parked; their sparse avatar motion stays behind.
void drive_on(game_state& state, const cart_pose& parked, const float forward) {
    room_ride ride;
    ride.owner = other;
    ride.motion.mode = motion_mode::cart;
    ride.motion.position = seat_position(parked, cart_seat::driver, state.tuning.cart) + glm::vec3(0.0f, 0.0f, forward);
    ride.motion.yaw = parked.yaw;
    ride.motion_at = state.online.server_now + static_cast<std::int64_t>(forward * 1000.0f) + 1;
    state.online.ride = ride;
}

bool sat_in_shown_seat(const game_state& state) {
    const cart_pose cart = *shown_cart(state, other);
    return near(horizontal(state.player.position), horizontal(seat_position(cart, cart_seat::passenger, state.tuning.cart)), 0.001f);
}
}

TEST_CASE("live world: with the tee book open my shot, another's shot and item uses finish, the book stays open") {
    game_state state = online_course();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    my_shot_playing(state);
    game_input smoke;
    smoke.use_smokable = true;
    update_game(state, smoke, 0.02f);
    REQUIRE(state.smoking.has_value());
    shoots(state, other);
    state.online.item_uses.push_back(room_item_use{other, state.default_loadout.drinkable});
    state.hub_flow.screen = hub_screen::book;
    state.net_commands.clear();

    const float use_seconds = std::max(state.items.find(state.smoking->item)->use->seconds_per_use,
                                       state.items.find(state.default_loadout.drinkable)->use->seconds_per_use);
    for (int i = 0; i < 4000 && (state.remote_shots.count(other) == 0U || remote_shot_playing(state.remote_shots.at(other))); ++i) {
        app_frame(state, false);
    }
    REQUIRE(state.remote_shots.count(other) == 1U);
    CHECK(!remote_shot_playing(state.remote_shots.at(other)));
    for (int i = 0; i < 4000 && (shot_playing(state) || state.smoking); ++i) {
        app_frame(state, false);
    }
    run(state, use_seconds);
    CHECK(!shot_playing(state));
    CHECK(near(horizontal(state.ball->position), horizontal(state.online.balls[me].position), 0.001f));
    CHECK(!state.smoking.has_value());
    CHECK(!state.remote_avatars.at(other).drinking.has_value());
    CHECK(state.hub_flow.screen == hub_screen::book);
    // Nothing was begun again: no shot, item or interaction went out.
    CHECK(count_commands(state, net_command_type::take_shot) == 0U);
    CHECK(count_commands(state, net_command_type::use_item) == 0U);
    CHECK(count_commands(state, net_command_type::place_bag) == 0U);
}

TEST_CASE("live world: a preparation the server has yet to answer holds my input and motion, not the room") {
    game_state state = online_course();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    shoots(state, other);
    state.online.room_ready = true;
    state.hub_flow.ball_request = net_command_type::tee_up;
    state.hub_flow.ball_updates = state.online.ball_updates;
    state.net_commands.clear();
    const glm::vec3 standing = state.player.position;

    game_input walk;
    walk.forward_held = true;
    walk.interact = true;
    walk.move_bag = true;
    for (int i = 0; i < 4000 && (state.remote_shots.count(other) == 0U || remote_shot_playing(state.remote_shots.at(other))); ++i) {
        update_game(state, walk, 0.02f);
    }
    REQUIRE(state.remote_shots.count(other) == 1U);
    CHECK(!remote_shot_playing(state.remote_shots.at(other)));
    CHECK(state.player.position == standing);
    CHECK(state.net_commands.empty());
    CHECK(state.hub_flow.ball_request.has_value());
}

TEST_CASE("live world: waiting for my group my bag handling finishes") {
    game_state state = online_course();
    waiting_on_group(state);

    // My bag off my cart onto my back: the handling plays out.
    state.online.bags[me] = room_bag{state.bag, state.clubs[state.selected_club].id};
    update_game(state, game_input{}, 0.02f);
    state.player.position = bag_position(state);
    game_input b;
    b.move_bag = true;
    update_game(state, b, 0.02f);
    REQUIRE(state.handling.has_value());
    state.online.bags[me].place = state.bag;  // the server takes the move
    run(state, handling_seconds(*state.handling, state.tuning.player) + 0.1f);
    CHECK(!state.handling.has_value());
    CHECK(bag_on_back(state));
    CHECK(count_commands(state, net_command_type::place_bag) == 1U);
}

TEST_CASE("live world: waiting for my group my righting of a cart finishes") {
    game_state state = online_course();
    waiting_on_group(state);
    state.net_commands.clear();

    // Another's cart on its side beside me: F rights it.
    const cart_pose parked{state.player.position + glm::vec3(0.0f, 0.0f, 12.0f), 0.0f};
    state.online.carts[other] = room_cart{parked.position, parked.yaw, 0, cart_condition::on_left_side};
    stand_at_door(state, parked, cart_seat::passenger);
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    REQUIRE(near_me->kind == interactable_kind::tipped_cart);
    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.righting.has_value());
    run(state, cart_spec_of(state, state.default_loadout.cart).right_seconds + 0.1f);
    CHECK(!state.righting.has_value());
    CHECK(count_commands(state, net_command_type::right_cart) == 1U);
}

TEST_CASE("live world: waiting for my group I ride on in my driver's passenger seat") {
    game_state state = online_course();
    waiting_on_group(state);
    const cart_pose parked{state.player.position + glm::vec3(0.0f, 0.0f, 12.0f), 0.0f};
    riding_with_other(state, parked);
    drive_on(state, parked, 0.0f);
    update_game(state, game_input{}, 0.02f);
    REQUIRE(waiting_for_group(state));
    drive_on(state, parked, 8.0f);
    run(state, state.tuning.net.ride_correction_seconds + 0.1f);
    CHECK(sat_in_shown_seat(state));
    CHECK(horizontal_distance(shown_cart(state, other)->position, parked.position) > 7.0f);
}

TEST_CASE("live world: app updates the online world under the results and the pause menu, never offline") {
    game_state state = online_course();
    waiting_on_group(state);
    const cart_pose parked{state.player.position + glm::vec3(0.0f, 0.0f, 12.0f), 0.0f};
    riding_with_other(state, parked);
    drive_on(state, parked, 0.0f);
    update_game(state, game_input{}, 0.02f);
    // They give their hole up: my results show.
    state.online.balls.erase(other);
    REQUIRE(round_results_shown(state));
    const play_frame results = play_frame_for(state, false, false);
    CHECK(results.screen == play_screen::results);
    CHECK(results.world_updates);

    // Someone outside my group shoots, and my driver takes me on, under the results.
    constexpr std::uint64_t stranger = 5;
    shoots(state, stranger);
    drive_on(state, parked, 8.0f);
    for (int i = 0; i < 4000 && (state.remote_shots.count(stranger) == 0U || remote_shot_playing(state.remote_shots.at(stranger))); ++i) {
        app_frame(state, false);
    }
    REQUIRE(round_results_shown(state));
    REQUIRE(state.remote_shots.count(stranger) == 1U);
    CHECK(!remote_shot_playing(state.remote_shots.at(stranger)));
    CHECK(sat_in_shown_seat(state));
    CHECK(horizontal_distance(shown_cart(state, other)->position, parked.position) > 7.0f);

    // Offline the results and the pause menu hold the world still.
    game_state offline = started_game(fixture_hub_course());
    for (std::size_t i = 0; i < offline.course_holes.size(); ++i) {
        tee_up(offline, i, false);
        offline.ball->strokes = 3;
        complete_current_hole(offline);
        pick_up_from_cup(offline);
    }
    REQUIRE(round_results_shown(offline));
    CHECK(play_frame_for(offline, false, false).screen == play_screen::results);
    CHECK(!play_frame_for(offline, false, false).world_updates);
    offline.results_dismissed = true;
    CHECK(play_frame_for(offline, true, false).screen == play_screen::pause);
    CHECK(!play_frame_for(offline, true, false).world_updates);
    CHECK(play_frame_for(offline, false, true).screen == play_screen::pause);
    CHECK(play_frame_for(offline, false, false).screen == play_screen::round);
}

TEST_CASE("live world: under the online pause menu my swing waits at its top for the server's answer, then plays") {
    game_state state = online_course();
    tee_up(state, 0, false);
    state.online.balls[me] = room_ball{0, state.ball->position, 0, false};
    state.online.players[me].motion = current_motion(state);
    update_game(state, game_input{}, 0.0f);
    address_looking_down(state);
    start_swing_on_time(state);
    REQUIRE(state.address.swing_elapsed.has_value());
    REQUIRE(count_commands(state, net_command_type::take_shot) == 1U);

    const play_frame paused = play_frame_for(state, true, false);
    CHECK(paused.screen == play_screen::pause);
    CHECK(paused.world_updates);
    for (int i = 0; i < 50; ++i) {
        app_frame(state, true);
    }
    CHECK(state.mode == game_mode::addressing);
    CHECK(state.address.swing_elapsed.has_value());
    answer_my_swing(state);
    for (int i = 0; i < 4000 && (state.mode == game_mode::addressing || shot_playing(state)); ++i) {
        app_frame(state, true);
    }
    CHECK(!shot_playing(state));
    CHECK(state.ball->strokes == 1);
    CHECK(count_commands(state, net_command_type::take_shot) == 1U);
}

TEST_CASE("live world: travel and an unready room freeze everything until the destination's snapshot") {
    game_state state = online_course();
    my_shot_playing(state);
    const float elapsed = state.shot->elapsed;
    state.hub_flow.travel_course = "hub";
    CHECK(play_frame_for(state, false, false).screen == play_screen::hub_menu);
    run(state, 0.5f);
    CHECK(state.shot->elapsed == elapsed);

    state.hub_flow.travel_course.clear();
    state.online.room = online_room{7, "course", 1, 1, place_kind::course};
    state.online.room_ready = false;
    run(state, 0.5f);
    CHECK(state.shot->elapsed == elapsed);
    state.online.room_ready = true;
    update_game(state, game_input{}, 0.02f);
    CHECK(state.shot->elapsed > elapsed);
}
