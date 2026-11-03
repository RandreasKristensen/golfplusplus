#include "doctest.h"

#include "game/body_contact.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/motion_sync.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/server_errors.h"
#include "movement_rules.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <cmath>
#include <vector>

namespace {
constexpr std::uint64_t me = 1;

game_input walking_forward() {
    game_input input;
    input.forward_held = true;
    return input;
}

game_state online_walker() {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = me;
    state.online.room_ready = true;
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    room_player self;
    self.motion = current_motion(state);
    state.online.players[me] = self;
    return state;
}

// The server's view of the room `state` plays in: its own cart, nobody else.
move_world server_world(const game_state& state) {
    move_world world{state.area, state.world.cart_roads, state.tuning};
    world.walk_speed = state.tuning.player.walk_speed;
    world.spec = my_cart_spec(state);
    world.wetness = state.weather.wetness;
    world.limits = movement_limits_for(state.tuning, world.spec, {}, 0);
    world.walker_obstacles = walking_obstacles(state);
    world.cart_obstacles = cart_obstacles(state);
    return world;
}

mover_state joined_at(const glm::vec3& position, const game_tuning& tuning) {
    mover_state mover = placed_mover(mover_state{}, position);
    mover.credit_seconds = tuning.server.max_motion_gap_seconds;
    mover.slack_left = tuning.server.motion_distance_slack;
    return mover;
}

std::vector<net_command> motions_from(game_state& state) {
    std::vector<net_command> motions;
    for (const net_command& command : state.net_commands) {
        if (command.type == net_command_type::motion) {
            motions.push_back(command);
        }
    }
    state.net_commands.clear();
    return motions;
}

move_request request_of(const net_command& command, const double id) {
    net_motion motion = command.motion;
    motion.client_time = id;
    return move_request{motion, command.first_step, command.start, command.runs};
}

move_input walk_step_input(const float yaw) {
    move_input input;
    input.forward = 1;
    input.yaw = quantise_yaw(yaw);
    return input;
}
}

TEST_CASE("walking in fixed steps ends in the same place whatever the frames' length") {
    game_state short_frames = started_game(fixture_hub_course());
    game_state long_frames = short_frames;
    for (int i = 0; i < 180; ++i) {
        update_game(short_frames, walking_forward(), 1.0f / 144.0f);
    }
    for (int i = 0; i < 50; ++i) {
        update_game(long_frames, walking_forward(), 1.0f / 40.0f);
    }
    // 1.25 seconds both, in whole steps.
    CHECK(near(short_frames.player.position, long_frames.player.position, 0.0001f));
    CHECK(horizontal_distance(short_frames.player.position, started_game(fixture_hub_course()).player.position) > 1.0f);
}

TEST_CASE("steps go out as runs of the same input, each a quantised yaw and steering turn") {
    std::vector<move_run> runs;
    const move_input ahead = walk_step_input(0.3f);
    append_step(runs, ahead);
    append_step(runs, ahead);
    move_input hop = ahead;
    hop.jump = true;
    append_step(runs, hop);
    append_step(runs, ahead);
    append_step(runs, ahead);
    REQUIRE(runs.size() == 3U);
    CHECK(runs[0].steps == 2);
    CHECK(runs[1].steps == 1);  // a hop is its own step
    CHECK(runs[2].steps == 2);

    for (const float yaw : {-3.14159f, -1.0f, 0.0f, 0.5f, 3.14f}) {
        CHECK(std::abs(wrap_angle(yaw_of(quantise_yaw(yaw)) - yaw)) <= 0.0001f);
    }
    CHECK(yaw_of(quantise_yaw(3.14159f)) < 0.0f);  // a half turn wraps, never runs past it
    for (const float steer : {-1.0f, -0.3f, 0.0f, 0.77f, 1.0f}) {
        CHECK(std::abs(steer_of(quantise_steer(steer)) - steer) <= 0.0001f);
    }
    CHECK(steer_of(quantise_steer(5.0f)) == 1.0f);
}

TEST_CASE("the server plays my walking steps to exactly where I predicted them") {
    game_state state = online_walker();
    const mover_state joined = joined_at(state.player.position, state.tuning);
    game_input input = walking_forward();
    for (int i = 0; i < 90; ++i) {
        input.strafe_left_held = (i / 20) % 2 == 0;
        state.player.yaw += 0.01f;
        update_game(state, input, 1.0f / 60.0f);
    }
    send_motion_now(state);
    const std::vector<net_command> motions = motions_from(state);
    REQUIRE(motions.size() >= 2U);
    mover_state mover = joined;
    double id = 1.0;
    float walked = 0.0f;
    for (const net_command& command : motions) {
        REQUIRE(!command.runs.empty());
        const move_outcome moved = play_move(mover, state.online.carts.at(me), me, request_of(command, id += 1.0), 1.0f,
                                             server_world(state));
        REQUIRE(moved.error.empty());
        // Each motion's claim is where its own steps left me.
        CHECK(near(moved.shown.position, command.motion.position, 0.00001f));
        mover = moved.mover;
        walked += moved.walked_meters;
    }
    CHECK(near(mover.position, state.player.position, 0.00001f));
    CHECK(walked > 0.5f * state.tuning.player.walk_speed);
}

TEST_CASE("a motion sent again changes nothing, and steps sent again are not played twice") {
    game_state state = online_walker();
    const mover_state joined = joined_at(state.player.position, state.tuning);
    const move_world world = server_world(state);
    net_motion motion = current_motion(state);
    motion.mode = motion_mode::walk;
    motion.client_time = 5.0;
    const move_request walk{motion, 1, joined.position,
                            {move_run{walk_step_input(state.player.yaw), 60}}};
    const move_outcome first = play_move(joined, std::nullopt, me, walk, 1.0f, world);
    REQUIRE(first.error.empty());
    REQUIRE(first.mover.step == 60U);
    CHECK(horizontal_distance(first.mover.position, joined.position) > 1.0f);

    // The same motion again: nothing at all.
    const move_outcome again = play_move(first.mover, std::nullopt, me, walk, 1.0f, world);
    CHECK(again.repeated);
    CHECK(again.error.empty());
    CHECK(near(again.mover.position, first.mover.position));

    // A newer motion carrying the same steps: they were played already.
    move_request resent = walk;
    resent.motion.client_time = 6.0;
    const move_outcome replayed = play_move(first.mover, std::nullopt, me, resent, 1.0f, world);
    REQUIRE(replayed.error.empty());
    CHECK(near(replayed.mover.position, first.mover.position));
    CHECK(replayed.walked_meters == 0.0f);

    // An older motion is refused.
    resent.motion.client_time = 4.0;
    CHECK(play_move(first.mover, std::nullopt, me, resent, 1.0f, world).error == error_invalid_motion);
}

TEST_CASE("steps never received count as standing, and the next go on from where the server has me") {
    game_state state = online_walker();
    const mover_state joined = joined_at(state.player.position, state.tuning);
    net_motion motion = current_motion(state);
    motion.mode = motion_mode::walk;
    motion.client_time = 5.0;
    // Steps 1..30 lost; 31..60 arrive, claiming to start where 30 would have left me.
    const glm::vec3 claimed = joined.position + yaw_direction(state.player.yaw) * 1.5f;
    const move_request later{motion, 31, claimed, {move_run{walk_step_input(state.player.yaw), 30}}};
    const move_outcome moved = play_move(joined, std::nullopt, me, later, 1.0f, server_world(state));
    REQUIRE(moved.error.empty());
    CHECK(moved.mover.step == 60U);
    // Further than the tolerance: the server's own position, not the claim.
    const float walked = horizontal_distance(moved.mover.position, joined.position);
    CHECK(near(walked, 30.0f * move_step_seconds * state.tuning.player.walk_speed, 0.05f));
}

TEST_CASE("steps beyond the time saved up are passed over, so no input buys speed") {
    game_state state = online_walker();
    mover_state mover = joined_at(state.player.position, state.tuning);
    mover.credit_seconds = 0.0f;
    net_motion motion = current_motion(state);
    motion.mode = motion_mode::walk;
    motion.client_time = 5.0;
    move_input sprint = walk_step_input(state.player.yaw);
    sprint.run = true;
    // Three seconds of running sent after half a second.
    const move_request fast{motion, 1, mover.position, {move_run{sprint, 360}}};
    const move_outcome moved = play_move(mover, std::nullopt, me, fast, 0.5f, server_world(state));
    REQUIRE(moved.error.empty());
    CHECK(moved.mover.step == 360U);
    const float running = state.tuning.player.walk_speed * state.tuning.player.run_factor;
    CHECK(horizontal_distance(moved.mover.position, mover.position) <= running * 0.5f + 0.01f);

    // More steps than any motion holds are refused outright.
    const move_request flood{motion, 361, moved.mover.position,
                             {move_run{sprint, UINT16_MAX}, move_run{sprint, UINT16_MAX}}};
    CHECK(play_move(moved.mover, std::nullopt, me, flood, 1.0f, server_world(state)).error == error_invalid_motion);
}

TEST_CASE("steps on foot start where I was put when it is near the server's, else where the server has me") {
    game_state state = online_walker();
    const mover_state joined = joined_at(state.player.position, state.tuning);
    net_motion motion = current_motion(state);
    motion.mode = motion_mode::idle;
    motion.client_time = 5.0;
    move_input stand;
    stand.yaw = quantise_yaw(state.player.yaw);
    const glm::vec3 near_by = joined.position + glm::vec3(0.8f * state.tuning.server.step_start_tolerance, 0.0f, 0.0f);
    const move_outcome put = play_move(joined, std::nullopt, me, move_request{motion, 1, near_by, {move_run{stand, 1}}}, 1.0f,
                                       server_world(state));
    REQUIRE(put.error.empty());
    CHECK(horizontal_distance(put.mover.position, near_by) < 0.001f);

    const glm::vec3 far_away = joined.position + glm::vec3(20.0f, 0.0f, 0.0f);
    const move_outcome kept = play_move(joined, std::nullopt, me, move_request{motion, 1, far_away, {move_run{stand, 1}}},
                                        1.0f, server_world(state));
    REQUIRE(kept.error.empty());
    CHECK(horizontal_distance(kept.mover.position, joined.position) < 0.001f);
    // A claim on foot moves nothing either.
    motion.position = far_away;
    motion.client_time = 6.0;
    const move_outcome claimed = play_move(kept.mover, std::nullopt, me, move_request{motion, 2, kept.mover.position, {}},
                                           1.0f, server_world(state));
    REQUIRE(claimed.error.empty());
    CHECK(horizontal_distance(claimed.shown.position, joined.position) < 0.001f);
}

TEST_CASE("addressing the ball is a claim the server checks: too far is refused, beside my resting ball is fine") {
    game_state state = online_walker();
    const mover_state joined = joined_at(state.player.position, state.tuning);
    move_world world = server_world(state);
    net_motion address = current_motion(state);
    address.mode = motion_mode::address;
    address.client_time = 5.0;
    address.position = joined.position + glm::vec3(300.0f, 0.0f, 0.0f);
    CHECK(play_move(joined, std::nullopt, me, move_request{address, 1, joined.position, {}}, 0.1f, world).error ==
          error_too_fast);
    address.position = joined.position + glm::vec3(0.5f, 0.0f, 0.0f);
    const move_outcome at_ball = play_move(joined, std::nullopt, me, move_request{address, 1, joined.position, {}}, 0.1f, world);
    REQUIRE(at_ball.error.empty());
    CHECK(near(at_ball.mover.position, address.position));
    CHECK(at_ball.mover.mode == motion_mode::address);
}

TEST_CASE("at the wheel the server steps my cart, refuses steps without the wheel, and walking takes me off it") {
    game_state state = online_walker();
    room_cart cart = state.online.carts.at(me);
    const mover_state joined = joined_at(step_out_position(cart_pose{cart.position, cart.yaw}, cart_seat::driver,
                                                           state.tuning.cart), state.tuning);
    const move_world world = server_world(state);
    net_motion driving = current_motion(state);
    driving.mode = motion_mode::cart;
    driving.client_time = 5.0;
    move_input pedal;
    pedal.kind = move_kind::cart;
    pedal.forward = 1;
    const move_request drive{driving, 1, joined.position, {move_run{pedal, 120}}};
    CHECK(play_move(joined, cart, me, drive, 1.0f, world).error == error_not_in_cart);

    cart.driver = me;
    const move_outcome driven = play_move(joined, cart, me, drive, 1.0f, world);
    REQUIRE(driven.error.empty());
    REQUIRE(driven.cart);
    CHECK(horizontal_distance(driven.cart->position, cart.position) > 1.0f);
    CHECK(driven.cart->motion_id == 5.0);
    CHECK(driven.shown.mode == motion_mode::cart);
    CHECK(glm::length(driven.mover.cart_velocity) > 0.5f);
    CHECK(near(driven.mover.position.x, seat_position(cart_pose{driven.cart->position, driven.cart->yaw}, cart_seat::driver,
                                                      state.tuning.cart).x, 0.001f));

    // Walking steps after it: off the wheel, from beside its driver's door.
    net_motion on_foot = driving;
    on_foot.mode = motion_mode::walk;
    on_foot.client_time = 6.0;
    const move_request walk{on_foot, 121, driven.mover.position, {move_run{walk_step_input(0.0f), 1}}};
    const move_outcome walked = play_move(driven.mover, driven.cart, me, walk, 0.1f, world);
    REQUIRE(walked.error.empty());
    REQUIRE(walked.cart);
    CHECK(walked.cart->driver == 0U);
    const glm::vec3 door = step_out_position(cart_pose{walked.cart->position, walked.cart->yaw}, cart_seat::driver,
                                             state.tuning.cart);
    CHECK(horizontal_distance(walked.mover.position, door) < 0.2f);
    CHECK(glm::length(walked.mover.cart_velocity) == 0.0f);
}

TEST_CASE("the server's own steps tip my cart over on a slope too steep, and put me and my rider beside it") {
    game_state state = online_walker();
    state.area = play_area{};
    state.world.cart_roads.clear();
    room_cart cart{glm::vec3(0.0f), 0.0f, 7, cart_condition::upright, me, 0.0};
    const mover_state joined = joined_at(seat_position(cart_pose{cart.position, cart.yaw}, cart_seat::driver,
                                                       state.tuning.cart), state.tuning);
    move_world world = server_world(state);
    world.walker_obstacles.clear();
    world.cart_obstacles.clear();
    world.grip_scale = 3.0f;  // grip enough that the turn lifts it rather than sliding
    net_motion driving = current_motion(state);
    driving.mode = motion_mode::cart;
    driving.client_time = 5.0;
    move_input hard_turn;
    hard_turn.kind = move_kind::cart;
    hard_turn.forward = 1;
    hard_turn.steer = quantise_steer(1.0f);
    const move_outcome over = play_move(joined, cart, me, move_request{driving, 1, joined.position, {move_run{hard_turn, 420}}},
                                        3.5f, world);
    REQUIRE(over.error.empty());
    REQUIRE(over.cart);
    CHECK(over.overturned);
    CHECK(cart_tipped(over.cart->condition));
    CHECK(over.cart->driver == 0U);
    CHECK(over.cart->passenger == 0U);
    CHECK(over.shown.mode == motion_mode::idle);
    const glm::vec3 door = step_out_position(cart_pose{over.cart->position, over.cart->yaw}, cart_seat::driver,
                                             state.tuning.cart);
    CHECK(horizontal_distance(over.mover.position, door) < 0.001f);
}

TEST_CASE("movement XP is paid for at most the hour's meters") {
    float used = 0.0f;
    CHECK(earning_meters(300.0f, used, 1000.0f) == 300.0f);
    CHECK(earning_meters(900.0f, used, 1000.0f) == 700.0f);
    CHECK(used == 1000.0f);
    CHECK(earning_meters(50.0f, used, 1000.0f) == 0.0f);
    CHECK(earning_meters(-5.0f, used, 1000.0f) == 0.0f);
    // The shipped caps leave steady play its XP: an hour of walking earns in full.
    const server_tuning& server = shipped_content().tuning.server;
    CHECK(server.walk_xp_meters_per_hour >= shipped_content().tuning.player.walk_speed * 3600.0f);
    CHECK(server.cart_xp_meters_per_hour > 0.0f);
    CHECK(motion_mode_count == 6U);
}
