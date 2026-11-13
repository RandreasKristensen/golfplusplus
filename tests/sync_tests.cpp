#include "doctest.h"

#include "sync_harness.h"
#include "game/address_play.h"
#include "game/body_contact.h"
#include "game/group_round.h"
#include "game/online_play.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <algorithm>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace {
game_input forward() { game_input input; input.forward_held = true; return input; }
}

TEST_CASE("sync: dismissing completed results lets the server follow my walk to the bus") {
    sync_harness link;
    for (std::size_t hole = 0; hole < link.client.round->strokes.size(); ++hole) {
        link.client.round = complete_hole(*link.client.round, hole, 3);
    }
    const auto scores = link.client.round->strokes;
    REQUIRE(round_results_shown(link.client));
    const auto before = link.mover.position;
    link.run(0.5f, forward());
    CHECK(near(link.mover.position, before));

    link.client.results_dismissed = true;
    link.run(1.0f, forward());
    link.run(0.5f);
    CHECK(horizontal_distance(before, link.mover.position) > 0.5f);
    CHECK(near(link.client.player.position, link.mover.position, 0.1f));
    CHECK(link.client.round->strokes == scores);
    CHECK(link.refusals.empty());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: walking after a long idle sends accepted steps without a refusal") {
    sync_harness link;
    link.run(25.0f);
    const auto before = link.mover.position;
    link.run(1.0f, forward());
    link.run(0.5f);
    CHECK(link.refusals.empty());
    CHECK(horizontal_distance(before, link.mover.position) > 0.5f);
    CHECK(link.client.motion_sync.breaches == 0);
    CHECK(near(link.client.player.position, link.mover.position, 0.1f));
}

TEST_CASE("sync: no online cart exists before its row arrives") {
    sync_harness link;
    link.client.online.carts.clear();
    CHECK(!own_cart(link.client));
    CHECK(!shown_cart(link.client, 0));
    CHECK(carts_in_world(link.client).empty());
    CHECK(!seat_door_at(link.client));
}

TEST_CASE("sync: yesterday's divergent parked cart starts from its row and confirms the wheel") {
    sync_harness link;
    link.client.cart.pose.position += glm::vec3(30.0f, 0.0f, 20.0f);
    CHECK(near(shown_cart(link.client, 0)->position, link.cart.position));
    link.enter();
    REQUIRE(driving_cart(link.client));
    CHECK(link.client.ride->confirmed);
    CHECK(link.cart.driver == 1);
    CHECK(std::find(link.refusals.begin(), link.refusals.end(), error_too_far) == link.refusals.end());
    CHECK(link.invariant());
    link.run(1.5f, forward());
    link.exit();
    link.run(0.5f);
    CHECK(link.cart.driver == 0);
    CHECK(link.agrees());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: a refused driving motion and a refused final motion restore the row") {
    for (const bool final : {false, true}) {
        sync_harness link;
        link.enter();
        REQUIRE(driving_cart(link.client));
        link.run(0.8f, forward());
        link.refuse_next_motion = error_too_fast;
        if (final) { link.exit(); }
        else { link.run(0.5f, forward()); link.exit(); }
        link.run(0.6f);
        CHECK(std::find(link.refusals.begin(), link.refusals.end(), error_too_fast) != link.refusals.end());
        CHECK(link.invariant());
        CHECK(link.agrees());
    }
}

TEST_CASE("sync: a lost stopping motion is repaired by a standing driver's heartbeat") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.run(1.0f, forward());
    link.drop_next_motion = true;
    link.run(2.8f);
    CHECK(driving_cart(link.client));
    CHECK(link.agrees());
    CHECK(link.invariant());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: a lost exit followed by disconnect and reconnect cannot retain a local cart") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.run(0.5f, forward());
    link.drop_next_motion = true;
    link.exit();
    link.disconnect();
    link.run(0.4f);
    CHECK(!shown_cart(link.client, 0));
    link.reconnect();
    CHECK(link.invariant());
    CHECK(link.agrees());
}

TEST_CASE("sync: a tow and another player's righting use their complete authoritative rows") {
    sync_harness link;
    link.cart.condition = cart_condition::sunk;
    link.publish(); link.run(0.2f);
    CHECK(cart_condition_of(link.client, 0) == cart_condition::sunk);
    const cart_pose elsewhere{glm::vec3(10.0f, 0.0f, 3.0f), 0.4f};
    const cart_decision tow = tow_cart_rule(link.cart, elsewhere);
    REQUIRE(tow.cart);
    link.cart = *tow.cart;
    link.publish(); link.run(0.4f);
    CHECK(link.agrees());
    link.cart.condition = cart_condition::on_left_side;
    link.publish(); link.run(0.2f);
    const cart_decision right = right_cart_rule(link.cart, link.cart.position, link.client.tuning);
    REQUIRE(right.cart);
    link.cart = *right.cart;
    link.publish(); link.run(0.2f);
    CHECK(link.agrees());
    CHECK(cart_condition_of(link.client, 0) == cart_condition::upright);
}

TEST_CASE("sync: the server's wheel and steps refuse impossible transitions and stop at solids") {
    sync_harness link;
    CHECK(take_wheel_rule(link.cart, 1, glm::vec3(1000.0f), link.client.tuning).error == error_too_far);
    // Steps at a wheel the server has not given are refused whole.
    move_input pedal;
    pedal.kind = move_kind::cart;
    pedal.forward = 1;
    net_motion driving = link.avatar;
    driving.mode = motion_mode::cart;
    driving.client_time = 50.0;
    const move_request unseated{driving, link.mover.step + 1, link.mover.position, {move_run{pedal, 60}}};
    CHECK(play_move(link.mover, link.cart, 1, unseated, 1.0f, link.world()).error == error_not_in_cart);
    link.enter();
    REQUIRE(driving_cart(link.client));
    CHECK(take_wheel_rule(link.cart, 2, link.cart.position, link.client.tuning).error == error_seat_taken);
    // Driving into a wall, the server's own steps stop at it.
    solid wall;
    wall.base = link.cart.position + yaw_direction(link.cart.yaw) * 4.0f;
    wall.radius = 2.0f;
    wall.height = 4.0f;
    wall.stops_cart = true;
    link.client.area.solids.push_back(wall);
    link.client.area.solids_grid = build_solid_grid(link.client.area.solids, 8.0f);
    driving.client_time = link.mover.client_time + 1.0;
    const move_request into{driving, link.mover.step + 1, link.mover.position, {move_run{pedal, 240}}};
    const move_outcome stopped = play_move(link.mover, link.cart, 1, into, 3.0f, link.world());
    REQUIRE(stopped.error.empty());
    REQUIRE(stopped.cart);
    CHECK(horizontal_distance(stopped.cart->position, wall.base) >= wall.radius);
    CHECK(link.invariant());
}

TEST_CASE("sync: cart acknowledgement corrects only the disagreement and replays later fixed steps") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.run(0.8f, forward());
    // Hold the link, retaining actual fixed inputs after an acknowledged snapshot.
    const net_motion sent = current_motion(link.client);
    net_command command;
    command.motion = sent;
    command.last_step = link.client.motion_sync.step;
    const double id = 900.0;
    motion_sent(link.client, command, id);
    for (int i = 0; i < 10; ++i) { update_game(link.client, forward(), 0.02f); }
    const glm::vec3 predicted = link.client.cart.pose.position;
    room_cart& row = link.client.online.carts.at(1);
    const cart_pose at_send = cart_around_seat(sent.position, sent.yaw, cart_seat::driver, link.client.tuning.cart);
    row.position = at_send.position + glm::vec3(0.25f, 0.0f, 0.0f);
    row.yaw = at_send.yaw;
    row.motion_id = id;
    row.driver = 1;
    link.client.online.players.at(1).motion = sent;
    link.client.online.players.at(1).motion->client_time = id;
    reconcile_motion(link.client, 0.0f);
    CHECK(near(link.client.cart.pose.position.x, predicted.x + 0.25f, 0.03f));
    CHECK(near(link.client.cart.pose.position.z, predicted.z, 0.03f));
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: unacknowledged predictions time out, repair and report once") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.loss = 1.0f;
    link.run(4.0f, forward());
    CHECK(link.client.motion_sync.breaches > 0);
    CHECK(!link.client.motion_sync.reports.empty());
    CHECK(link.invariant());
}

TEST_CASE("sync: seeded three minute soak with five percent loss and 200ms jitter converges for both clients") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.loss = 0.05f;
    link.jitter = 0.2f;
    std::uint32_t random = 991;
    for (int frame = 0; frame < 9000; ++frame) {
        random = random * 1664525U + 1013904223U;
        game_input input;
        input.forward_held = (frame / 100) % 4 != 0;
        input.strafe_left_held = (random >> 27) == 0;
        input.strafe_right_held = (random >> 27) == 1;
        link.tick(input);
        REQUIRE(link.invariant());
    }
    link.loss = 0.0f;
    link.jitter = 0.0f;
    link.run(3.0f);
    link.exit();
    link.run(0.5f);
    CHECK(link.agrees());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: a lost final driving motion leaves no outstanding idle cart prediction") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.run(0.7f, forward());
    link.drop_next_motion = true;
    link.exit();
    link.run(4.0f);
    CHECK(link.agrees());
    CHECK(link.client.motion_sync.cart_steps.empty());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: delayed wheel confirmation cannot move the cart and a refusal restores the entry origin") {
    sync_harness link;
    link.latency = 0.6f;
    stand_at_door(link.client, *shown_cart(link.client, 0), cart_seat::driver);
    game_input input = forward();
    for (int i = 0; i < 50 && on_foot(link.client); ++i) { link.tick(input); }
    REQUIRE(link.client.ride);
    REQUIRE(link.client.ride->step);
    const glm::vec3 from = link.client.ride->step->from;
    const glm::vec3 parked = link.cart.position;
    link.run(0.7f, forward());
    CHECK(!driving_cart(link.client));
    CHECK(near(link.client.cart.pose.position, parked));
    // A delayed denial still knows where the entry started after its animation is over.
    link.client.online.refusals.push_back({reducer_take_wheel, error_too_far});
    update_online_play(link.client, 0.0f);
    CHECK(on_foot(link.client));
    CHECK(near(link.client.player.position, from));
}

TEST_CASE("sync: a walking acknowledgement keeps movement after its id and repairs the rejected origin") {
    sync_harness link;
    net_command sent;
    sent.motion = current_motion(link.client);
    sent.motion.mode = motion_mode::walk;
    sent.motion.speed = 1.0f;
    motion_sent(link.client, sent, 20.0);
    link.client.player.position += glm::vec3(0.0f, 0.0f, 0.5f);
    net_motion authoritative = sent.motion;
    authoritative.client_time = 20.0;
    authoritative.position.x += 0.2f;
    link.client.online.players.at(1).motion = authoritative;
    reconcile_motion(link.client, 0.0f);
    CHECK(near(link.client.player.position.x, authoritative.position.x));
    CHECK(near(link.client.player.position.z, authoritative.position.z + 0.5f));
    CHECK(link.client.motion_sync.history.empty());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: jitter-bunched driving motions heal for both clients") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.jitter = 0.2f;
    link.mover.slack_left = 0.0f;
    link.run(8.0f, forward());
    link.jitter = 0.0f;
    link.run(2.5f);
    link.exit();
    link.run(0.5f);
    CHECK(link.invariant());
    CHECK(link.agrees());
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: delayed dispatch retains the cart state that actually produced the queued motion") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.client.net_commands.clear();
    send_motion_now(link.client);
    REQUIRE(link.client.net_commands.size() == 1);
    const net_command command = link.client.net_commands.front();
    REQUIRE(command.cart_prediction);
    const glm::vec2 velocity = command.cart_prediction->motion.velocity;
    link.client.cart.motion.velocity += glm::vec2(1.0f, 2.0f);
    motion_sent(link.client, command, 800.0);
    REQUIRE(!link.client.motion_sync.history.empty());
    CHECK(link.client.motion_sync.history.back().cart.motion.velocity == velocity);
}

TEST_CASE("sync: stepping out blends the drawn cart towards its row with the correct screen direction and yaw") {
    sync_harness link;
    link.enter();
    REQUIRE(driving_cart(link.client));
    link.client.cart.pose.position.x += 2.0f;
    link.client.cart.pose.yaw = 0.3f;
    const cart_pose predicted = link.client.cart.pose;
    const glm::mat4 camera = glm::perspective(glm::radians(60.0f), 4.0f / 3.0f, 0.1f, 100.0f) *
        glm::lookAt(glm::vec3(0.0f, 4.0f, -12.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    const auto project = [&camera](const glm::vec3& point) {
        const glm::vec4 clip = camera * glm::vec4(point, 1.0f);
        return glm::vec2(clip) / clip.w;
    };
    game_input out;
    out.interact = true;
    update_cart(link.client, out, 0.0f, nullptr);
    const cart_pose early = carts_in_world(link.client).front().pose;
    CHECK(glm::length(project(early.position) - project(predicted.position)) < 0.0001f);
    CHECK(near(early.yaw, predicted.yaw));
    CHECK(near(shown_cart(link.client, 0)->position, link.client.online.carts.at(1).position));
    const float half = link.client.tuning.net.remote_correction_seconds * 0.5f;
    update_carts(link.client, half);
    const cart_pose middle = carts_in_world(link.client).front().pose;
    update_carts(link.client, half);
    const cart_pose end = carts_in_world(link.client).front().pose;
    CHECK(project(early.position).x < project(middle.position).x);
    CHECK(project(middle.position).x < project(end.position).x);
    const glm::vec3 front = middle.position + yaw_direction(middle.yaw);
    CHECK(project(front).x < project(middle.position + yaw_direction(end.yaw)).x);
    CHECK(middle.yaw > end.yaw);
    CHECK(middle.yaw < early.yaw);
    CHECK(glm::length(project(end.position) - project(link.client.online.carts.at(1).position)) < 0.0001f);
}

TEST_CASE("sync: others follow every small turn of my aim at my ball") {
    sync_harness link;
    // My ball at rest in front of me, my bag on my cart: I step up to it.
    const glm::vec3 ball = link.client.player.position + yaw_direction(link.client.player.yaw) * 0.5f;
    const std::string club = link.client.clubs[link.client.selected_club].id;
    link.client.ball = my_ball{0, ball, 1, ball_place::at_rest};
    link.ball = ball;
    link.observer.online.balls[1] = room_ball{0, ball, 1};
    link.observer.online.bags[1] = room_bag{link.client.bag, club};
    step_up_to_ball(link.client);
    REQUIRE(link.client.mode == game_mode::addressing);
    link.run(0.6f);
    REQUIRE(remote_address_pose(link.observer, 1).has_value());
    game_input left;
    left.strafe_left_held = true;
    for (int turn = 0; turn < 6; ++turn) {
        const float before = link.client.address.setup.aim_angle;
        link.tick(left);
        link.run(0.6f);
        const float turned = std::abs(wrap_angle(link.client.address.setup.aim_angle - before));
        REQUIRE(turned > 0.0f);
        REQUIRE(turned < link.client.tuning.net.motion_correction_yaw);
        const std::optional<remote_address> theirs = remote_address_pose(link.observer, 1);
        REQUIRE(theirs.has_value());
        CHECK(near(theirs->setup.aim_angle, link.client.address.setup.aim_angle, 0.001f));
        CHECK(theirs->setup.club_id == club);
    }
    CHECK(link.refusals.empty());
}


TEST_CASE("sync: walking correction preserves later pacing and crouched steps") {
    sync_harness link;
    link.client.player.yaw = 0.0f;
    net_command sent;
    sent.motion = current_motion(link.client);
    sent.motion.mode = motion_mode::walk;
    motion_sent(link.client, sent, 20.0);
    link.client.net_commands.clear();
    const glm::vec3 from = link.client.player.position;
    game_input pace = forward();
    pace.toggle_pacing = true;
    update_game(link.client, pace, move_step_seconds);
    pace.toggle_pacing = false;
    update_game(link.client, pace, move_step_seconds);
    game_input crouch;
    crouch.crouch_held = true;
    crouch.strafe_left_held = true;
    update_game(link.client, crouch, move_step_seconds);
    const glm::vec3 travel = link.client.player.position - from;
    CHECK(near(travel.z, link.client.tuning.player.walk_speed * link.client.tuning.pacing.walk_factor *
                        2.0f * move_step_seconds, 0.00001f));
    CHECK(near(travel.x, link.client.tuning.player.walk_speed *
                        link.client.tuning.green_read.walk_factor * move_step_seconds, 0.00001f));
    net_motion authoritative = sent.motion;
    authoritative.client_time = 20.0;
    authoritative.position.x += 0.2f;
    link.client.online.players.at(1).motion = authoritative;
    reconcile_motion(link.client, 0.0f);
    CHECK(near(link.client.player.position, authoritative.position + travel, 0.00001f));
    CHECK(link.client.motion_sync.breaches == 0);
}

TEST_CASE("sync: slow walking styles stay authoritative through changing input and latency") {
    sync_harness link;
    link.latency = 0.08f;
    link.jitter = 0.04f;
    game_input pace = forward();
    pace.toggle_pacing = true;
    link.tick(pace);
    pace.toggle_pacing = false;
    link.run(0.6f, pace);
    game_input read = pace;
    read.crouch_held = true;
    read.strafe_left_held = true;
    link.run(0.6f, read);
    CHECK(link.avatar.crouched);
    link.run(0.6f, pace);
    pace.toggle_pacing = true;
    link.tick(pace);
    link.run(1.0f);
    CHECK(near(link.avatar.position, link.client.player.position, 0.001f));
    CHECK(link.refusals.empty());
    CHECK(link.client.motion_sync.breaches == 0);
}
