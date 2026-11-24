#include "doctest.h"

#include "sync_harness.h"
#include "flight_rules.h"
#include "game/address_play.h"
#include "game/step_timeline.h"
#include "game/body_contact.h"
#include "game/contact_prediction.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
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

namespace {
game_input running() { game_input input = forward(); input.run_held = true; return input; }

// A ball crossing the line of a golfer on foot (or driving, `drives`) from the side on the harness's flat ground,
// 60 m/s and 1 m up, shot 12 m away so as to arrive `crossing_s` after they start, at where they would be then (`into_line`)
// or, not, at where they stood when it was shot, 0.2 s before. The server plays each motion that gets through against the flight
// as the reducer does; the client looks for its own contact from its own steps as contact_prediction.h does.
struct crossing {
    bool drives = false;
    bool into_line = true;
    float latency = 0.12f;
    float jitter = 0.0f;
    float loss = 0.0f;
    std::uint32_t seed = 37;
    float crossing_s = 0.2f;
};

struct crossing_result {
    bool pending_at_swing = false;            // the provisional contact of standing where I stand
    std::optional<flight_contact> server;     // applied once settled
    std::optional<flight_contact> predicted;  // the client's first, from its own steps
    std::vector<mover_sample> played;         // what the server played of my path
    std::size_t refusals = 0;
    std::uint64_t breaches = 0;
    float final_offset = 0.0f;                // the server's me to the client's, after the link settles
    glm::vec3 stood{0.0f};
    float unit = 1.0f;
};

void set_up(sync_harness& link, const crossing& scenario) {
    link.latency = scenario.latency;
    link.random = scenario.seed;
    if (scenario.drives) {
        link.enter();  // the wheel is taken before the link gets bad
    }
    link.jitter = scenario.jitter;
    link.loss = scenario.loss;
}

glm::vec3 mine(const sync_harness& link, const bool drives) {
    return drives ? link.client.cart.pose.position : link.client.player.position;
}

crossing_result cross_the_flight(const crossing& scenario) {
    crossing_result result;
    sync_harness link;
    set_up(link, scenario);
    const game_tuning& tuning = link.client.tuning;
    result.unit = tuning.scale.meters_per_world_unit;
    const float unit = result.unit;
    const int frames = static_cast<int>(2.0f / 0.02f);

    // Where I am when, found by walking an identical link.
    sync_harness probe;
    set_up(probe, scenario);
    const std::size_t shot_frame = static_cast<std::size_t>(std::max(0.0f, scenario.crossing_s - 0.2f) / 0.02f);
    const std::int64_t shot_at = static_cast<std::int64_t>((probe.now + 0.02 * static_cast<double>(shot_frame)) * 1000.0);
    std::vector<glm::vec3> track{mine(probe, scenario.drives)};
    for (int i = 0; i < frames; ++i) {
        probe.tick(running());
        track.push_back(mine(probe, scenario.drives));
    }
    result.stood = track[shot_frame];
    const glm::vec3 ahead = safe_normalize(horizontal(track.back() - track.front()), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 across(-ahead.z, 0.0f, ahead.x);
    const glm::vec3 where = scenario.into_line ? track[shot_frame + 10] : result.stood;

    weather_state calm = link.client.weather;
    calm.wind = glm::vec3(0.0f);
    const shot_course course{link.client.area, shot_hole{}, calm, 0.0f, 0.0f};
    const flight_env env{course, tuning, link.client.clubs};
    live_flight live;
    live.record.setup.shot_at = shot_at;
    live.record.seed = 5;
    ball_state ball = still_ball(link.client);
    ball.position = where - across * (12.0f / unit) + glm::vec3(0.0f, 1.0f / unit, 0.0f);
    ball.velocity = across * (60.0f / unit);
    const shot_course flown = struck_course(live.record, course, tuning);
    live.flight = fly_ball(ball, shot_at, flown, tuning);

    const snapshot_kind kind = scenario.drives ? snapshot_kind::cart : snapshot_kind::person;
    const float yaw = scenario.drives ? link.client.cart.pose.yaw : 0.0f;
    const snapshot_entry standing{kind, result.stood, yaw, false, 1};
    live = flight_after_movement(std::move(live), standing_path(standing, shot_at - 500, 0), false, {}, env);
    result.pending_at_swing = !live.pending.empty();

    link.after_move = [&](const move_outcome& moved) {
        const std::vector<mover_sample>& samples = scenario.drives ? moved.cart_poses : moved.feet;
        if (samples.empty()) { return; }
        mover_path path;
        path.samples = samples;
        path.mover = snapshot_entry{kind, samples.back().position, samples.back().yaw, false, 1};
        result.played.insert(result.played.end(), samples.begin(), samples.end());
        live = flight_after_movement(std::move(live), std::move(path), false, {}, env);
    };
    const double known_from = static_cast<double>(shot_at) / 1000.0 + scenario.latency + scenario.jitter;
    for (int i = 0; i < frames + 25; ++i) {
        link.tick(i < frames ? running() : game_input{});
        if (!result.predicted && link.now >= known_from) {
            const snapshot_entry me{kind, glm::vec3(0.0f), 0.0f, false, 1};
            result.predicted =
                first_contact(live.flight, logged_path(link.client.motion_sync.path, me, shot_unix_ms(link.client)), flown, tuning);
        }
    }
    link.loss = 0.0f;
    link.jitter = 0.0f;
    link.run(3.0f);
    result.refusals = link.refusals.size();
    result.breaches = link.client.motion_sync.breaches;
    result.final_offset = horizontal_distance(scenario.drives ? link.cart.position : link.avatar.position, mine(link, scenario.drives));

    const settlement settled = settle_flight(live, shot_at + 600'000, {}, env);
    if (!settled.applied.empty()) {
        result.server = settled.applied.front().contact;
    }
    return result;
}

// The client's predicted hit and the server's contact are at the same moment, within what the ball blends over.
bool agree_within_blend(const flight_contact& predicted, const flight_contact& server, const game_tuning& tuning) {
    return std::abs(predicted.time - server.time) <= tuning.net.shot_correction_seconds;
}
}

TEST_CASE("sync: under latency a walker stepping out of the ball's line is not hit, one stepping into it is hit where their steps have them") {
    for (const bool walks_into_line : {false, true}) {
        crossing scenario;
        scenario.into_line = walks_into_line;
        const crossing_result run = cross_the_flight(scenario);
        CHECK(run.pending_at_swing == !walks_into_line);
        CHECK(run.refusals == 0U);
        CHECK(run.server.has_value() == walks_into_line);
        CHECK(run.predicted.has_value() == walks_into_line);
        if (walks_into_line) {
            REQUIRE(run.server);
            REQUIRE(run.predicted);
            CHECK(horizontal_distance(run.server->mover.position, run.stood) > 0.5f / run.unit);
            const flight_contact& met = *run.server;
            const bool a_played_step = std::any_of(run.played.begin(), run.played.end(), [&met](const mover_sample& sample) {
                return near(sample.position, met.mover.position, 0.0001f);
            });
            CHECK(a_played_step);
            sync_harness link;
            CHECK(agree_within_blend(*run.predicted, met, link.client.tuning));
        }
    }
}

TEST_CASE("sync: under 200 ms jitter and five percent loss a walker stepping out is not hit and converges, one stepping in is hit") {
    for (std::uint32_t seed = 3; seed < 9; ++seed) {
        crossing out;
        out.into_line = false;
        out.latency = 0.04f;
        out.jitter = 0.2f;
        out.loss = 0.05f;
        out.seed = seed;
        const crossing_result stepped_out = cross_the_flight(out);
        CHECK(!stepped_out.server.has_value());
        CHECK(!stepped_out.predicted.has_value());
        CHECK(stepped_out.refusals == 0U);
        CHECK(stepped_out.breaches == 0U);
        CHECK(stepped_out.final_offset < 0.1f);

        // Reordered but whole: the walker's steps reach the server in their own time, so the hit stands.
        crossing in = out;
        in.into_line = true;
        in.loss = 0.0f;
        const crossing_result stepped_in = cross_the_flight(in);
        REQUIRE(stepped_in.server);
        REQUIRE(stepped_in.predicted);
        CHECK(stepped_in.refusals == 0U);
        CHECK(stepped_in.breaches == 0U);
        sync_harness link;
        CHECK(agree_within_blend(*stepped_in.predicted, *stepped_in.server, link.client.tuning));
    }
}

TEST_CASE("sync: a driven cart crossing a flight is hit where its steps have it, one that has left the line is not") {
    for (const bool into_line : {false, true}) {
        crossing scenario;
        scenario.drives = true;
        scenario.into_line = into_line;
        scenario.crossing_s = 1.0f;
        scenario.latency = 0.04f;
        scenario.jitter = 0.1f;
        const crossing_result run = cross_the_flight(scenario);
        CHECK(run.refusals == 0U);
        CHECK(run.breaches == 0U);
        CHECK(run.server.has_value() == into_line);
        CHECK(run.predicted.has_value() == into_line);
        if (into_line) {
            REQUIRE(run.server);
            REQUIRE(run.predicted);
            CHECK(run.server->mover.kind == snapshot_kind::cart);
            CHECK(run.predicted->mover.kind == snapshot_kind::cart);
            const flight_contact& met = *run.server;
            const bool a_played_pose = std::any_of(run.played.begin(), run.played.end(), [&met](const mover_sample& sample) {
                return near(sample.position, met.mover.position, 0.0001f);
            });
            CHECK(a_played_pose);
            sync_harness link;
            CHECK(agree_within_blend(*run.predicted, met, link.client.tuning));
        }
    }
}

TEST_CASE("sync: a batch older than the rewind window counts as standing, so balls crossing between its steps miss, and a timely link never misses") {
    int missed_late = 0;
    int missed_timely = 0;
    int predicted_anyway = 0;
    for (int i = 0; i < 13; ++i) {
        crossing scenario;
        scenario.crossing_s = 1.0f + 0.02f * static_cast<float>(i);
        scenario.latency = 0.04f;
        missed_timely += cross_the_flight(scenario).server.has_value() ? 0 : 1;
        scenario.latency = 0.6f;
        const crossing_result late = cross_the_flight(scenario);
        missed_late += late.server.has_value() ? 0 : 1;
        predicted_anyway += late.predicted.has_value() ? 1 : 0;
        CHECK(late.refusals == 0U);
    }
    CHECK(missed_timely == 0);
    CHECK(missed_late > 0);
    // The client's own steps still meet the ball: only the server's contact is the one that counts.
    CHECK(predicted_anyway > 0);
}

TEST_CASE("sync: seeded soak of crossings under jitter, loss and reordering has no refusal, no breach and converges") {
    std::uint32_t random = 4242;
    for (std::uint32_t round = 0; round < 24; ++round) {
        random = random * 1664525U + 1013904223U;
        crossing scenario;
        scenario.seed = 100 + round;
        scenario.drives = (random >> 28) % 3 == 0;
        scenario.into_line = (random >> 24) % 2 == 0;
        scenario.latency = 0.04f + 0.02f * static_cast<float>((random >> 20) % 4);
        scenario.jitter = 0.2f;
        scenario.loss = (random >> 16) % 2 == 0 ? 0.05f : 0.0f;
        scenario.crossing_s = scenario.drives ? 1.0f : 0.6f + 0.1f * static_cast<float>((random >> 12) % 8);
        const crossing_result run = cross_the_flight(scenario);
        CHECK(run.refusals == 0U);
        CHECK(run.breaches == 0U);
        CHECK(run.final_offset < 0.1f);
        if (!scenario.into_line && !scenario.drives && scenario.loss == 0.0f) {
            CHECK(!run.server.has_value());
        }
    }
}
