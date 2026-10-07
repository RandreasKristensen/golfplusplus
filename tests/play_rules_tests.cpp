#include "doctest.h"

#include "game/net_types.h"
#include "physics/vector_math.h"
#include "play_rules.h"
#include "server_errors.h"

#include "server_test_support.h"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace {
std::uint8_t mode_number(const motion_mode mode) {
    return static_cast<std::uint8_t>(mode);
}

server_content fixture_server_content() {
    const content_fixture fixture = fixture_content_files();
    return *parse_server_content(fixture.files).content;
}

// Where a player stands to hit the ball.
glm::vec3 beside(const server_ball& ball) {
    return ball.position + glm::vec3(0.5f, 0.0f, 0.0f);
}

// Standing at `position` as the last accepted motion, in `mode`.
net_motion was(const glm::vec3& position, const std::uint8_t mode) {
    net_motion motion;
    motion.mode = static_cast<motion_mode>(mode);
    motion.position = position;
    return motion;
}

shot_request putt(const int stroke) {
    shot_request request;
    request.stroke = stroke;
    request.club_id = "putter";
    request.power = 0.5f;
    request.wind_time = 3.0f;
    return request;
}
}

TEST_CASE("rooms fill the fullest room with space, oldest first, else none") {
    CHECK(choose_room({}, 40) == std::nullopt);
    CHECK(choose_room({{1, 3}, {2, 39}, {3, 12}}, 40) == std::optional<std::uint64_t>(2));
    CHECK(choose_room({{1, 40}, {2, 40}}, 40) == std::nullopt);
    CHECK(choose_room({{5, 10}, {4, 10}}, 40) == std::optional<std::uint64_t>(4));
}

TEST_CASE("motion may not outrun its mode's speed, with slack that does not add up") {
    const game_tuning& tuning = shipped_content().tuning;
    const std::uint8_t walk = mode_number(motion_mode::walk);
    const std::uint8_t cart = mode_number(motion_mode::cart);
    const float slack = tuning.server.motion_distance_slack;
    const float walking = tuning.player.walk_speed * tuning.server.motion_speed_scale;
    const float driving = fastest_cart_speed(tuning.cart) * tuning.server.motion_speed_scale;
    const glm::vec3 start(0.0f);

    CHECK(check_motion(was(start, walk), glm::vec3(walking * 2.0f + slack - 0.01f, 50.0f, 0.0f), 2.0f, walk, slack, tuning).allowed);
    CHECK(!check_motion(was(start, walk), glm::vec3(walking * 2.0f + slack + 0.01f, 0.0f, 0.0f), 2.0f, walk, slack, tuning).allowed);
    // Getting into the cart allows cart speed from the start.
    CHECK(check_motion(was(start, walk), glm::vec3(driving * 2.0f, 0.0f, 0.0f), 2.0f, cart, slack, tuning).allowed);
    CHECK(!check_motion(was(start, walk), glm::vec3(0.0f, 0.0f, slack + 0.01f), -5.0f, walk, slack, tuning).allowed);

    // Quick calls each a little too far use the slack up instead of adding it.
    float slack_left = slack;
    int accepted = 0;
    glm::vec3 at = start;
    for (int i = 0; i < 100; ++i) {
        const glm::vec3 to = at + glm::vec3(walking * 0.01f + 1.0f, 0.0f, 0.0f);
        const motion_check check = check_motion(was(at, walk), to, 0.01f, walk, slack_left, tuning);
        if (check.allowed) {
            ++accepted;
            at = to;
        }
        slack_left = check.slack_left;
    }
    CHECK(accepted <= static_cast<int>(slack) + 2);
    CHECK(slack_left <= slack);
}

TEST_CASE("standing still does not save up a jump") {
    const game_tuning& tuning = shipped_content().tuning;
    const std::uint8_t idle = mode_number(motion_mode::idle);
    const std::uint8_t cart = mode_number(motion_mode::cart);
    const float slack = tuning.server.motion_distance_slack;
    const float gap = tuning.server.max_motion_gap_seconds;
    const float reach = fastest_cart_speed(tuning.cart) * tuning.server.motion_speed_scale * gap + slack;

    CHECK(check_motion(was(glm::vec3(0.0f), idle), glm::vec3(reach - 0.01f, 0.0f, 0.0f), 30.0f, cart, slack, tuning).allowed);
    CHECK(!check_motion(was(glm::vec3(0.0f), idle), glm::vec3(reach + 0.01f, 0.0f, 0.0f), 30.0f, cart, slack, tuning).allowed);
}

TEST_CASE("movement xp counts no more than the mode's own speed") {
    const game_tuning& tuning = shipped_content().tuning;
    const std::uint8_t walk = mode_number(motion_mode::walk);
    const float slack = tuning.server.motion_distance_slack;
    const float honest = tuning.player.walk_speed;

    const motion_check slow = check_motion(was(glm::vec3(0.0f), walk), glm::vec3(honest * 0.5f, 0.0f, 0.0f), 1.0f, walk, slack, tuning);
    CHECK(near(slow.earned_distance, honest * 0.5f, 0.0001f));
    const motion_check hurried =
        check_motion(was(glm::vec3(0.0f), walk), glm::vec3(honest * 1.3f, 0.0f, 0.0f), 1.0f, walk, slack, tuning);
    REQUIRE(hurried.allowed);
    CHECK(near(hurried.earned_distance, honest, 0.0001f));
    CHECK(!check_motion(was(glm::vec3(0.0f), walk), glm::vec3(1000.0f, 0.0f, 0.0f), 1.0f, walk, slack, tuning).earned_distance);

    // A stretch counts in the mode it was covered in: driving, then stopping.
    const std::uint8_t cart = mode_number(motion_mode::cart);
    const std::uint8_t idle = mode_number(motion_mode::idle);
    const float driven = tuning.cart.speed;
    const motion_check stop = check_motion(was(glm::vec3(0.0f), cart), glm::vec3(driven, 0.0f, 0.0f), 1.0f, idle, slack, tuning);
    REQUIRE(stop.allowed);
    CHECK(near(stop.earned_distance, driven, 0.0001f));
}

TEST_CASE("movement xp counts the path the last motion traced, turns included") {
    const game_tuning& tuning = shipped_content().tuning;
    const float slack = tuning.server.motion_distance_slack;
    net_motion circling = was(glm::vec3(0.0f), mode_number(motion_mode::walk));
    circling.speed = tuning.player.walk_speed;
    circling.turn_rate = tuning.player.turn_rate;
    const float seconds = 2.0f;
    const glm::vec3 arrived = extrapolate_motion(circling, seconds);
    REQUIRE(horizontal_distance(circling.position, arrived) < circling.speed * seconds - 0.1f);

    // Ending where the motion leads: the arc counts, not the chord.
    const motion_check walked = check_motion(circling, arrived, seconds, mode_number(motion_mode::walk), slack, tuning);
    REQUIRE(walked.allowed);
    CHECK(near(walked.earned_distance, circling.speed * seconds, 0.001f));

    // Ending somewhere else: only the straight line is known.
    const glm::vec3 elsewhere = circling.position + glm::vec3(0.0f, 0.0f, -1.0f);
    const motion_check strayed = check_motion(circling, elsewhere, seconds, mode_number(motion_mode::walk), slack, tuning);
    REQUIRE(strayed.allowed);
    CHECK(near(strayed.earned_distance, 1.0f, 0.001f));
}

TEST_CASE("movement earns walking on foot and cart xp only in a cart on a road") {
    CHECK(movement_earns(mode_number(motion_mode::walk), false).walking);
    CHECK(!movement_earns(mode_number(motion_mode::walk), true).cart);
    CHECK(!movement_earns(mode_number(motion_mode::cart), false).cart);
    const movement_earnings drift = movement_earns(mode_number(motion_mode::drift), true);
    CHECK(drift.cart);
    CHECK(drift.drift);
    CHECK(!drift.walking);
    CHECK(!movement_earns(mode_number(motion_mode::cart), true).drift);
    for (const motion_mode at : {motion_mode::aim, motion_mode::address, motion_mode::swing}) {
        const movement_earnings earnings = movement_earns(mode_number(at), true);
        CHECK(!earnings.walking);
        CHECK(!earnings.cart);
    }
    CHECK(motion_mode_count == 7U);
}

TEST_CASE("interaction reach is the radius plus slack, horizontally") {
    const server_tuning& tuning = shipped_content().tuning.server;
    const glm::vec3 target(10.0f, 0.0f, 0.0f);
    CHECK(within_interact_reach(glm::vec3(10.0f + 3.0f + tuning.interact_slack, 40.0f, 0.0f), target, 3.0f, tuning));
    CHECK(!within_interact_reach(glm::vec3(10.0f + 3.0f + tuning.interact_slack + 0.01f, 0.0f, 0.0f), target, 3.0f, tuning));
}

TEST_CASE("a valid shot starts from the server's ball, with power clamped") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(1.0f, 2.0f, 3.0f), 2, 1.0f};
    shot_request request = putt(3);
    request.power = 7.0f;
    request.cigarette_active = true;

    const shot_check lit = check_shot(request, ball, beside(ball), 5.0f, true, content);
    REQUIRE(lit.input.has_value());
    CHECK(lit.error.empty());
    CHECK(lit.input->ball_start == ball.position);
    CHECK(lit.input->power == 1.0f);
    CHECK(lit.input->cigarette_active);
    CHECK(lit.input->club_id == "putter");

    request.power = 0.0f;
    request.cigarette_active = false;
    const shot_check plain = check_shot(request, ball, beside(ball), 5.0f, false, content);
    REQUIRE(plain.input.has_value());
    CHECK(plain.input->power == content.tuning.swing.min_power);
    CHECK(!plain.input->cigarette_active);

    // A shot the client simulated with a cigarette the server says is out.
    request.cigarette_active = true;
    CHECK(check_shot(request, ball, beside(ball), 5.0f, false, content).error == error_cigarette_out);
}

TEST_CASE("aim must come wrapped into one turn") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(0.0f), 0, 0.0f};
    shot_request request = putt(1);
    for (const float aim : {-3.14159f, 0.0f, 3.14159f}) {
        request.aim_angle = aim;
        CHECK(check_shot(request, ball, beside(ball), 3.0f, false, content).input.has_value());
    }
    request.aim_angle = 7.0f;
    CHECK(check_shot(request, ball, beside(ball), 3.0f, false, content).error == error_invalid_shot);

    CHECK(near(wrap_angle(7.0f), 7.0f - 2.0f * glm::pi<float>(), 0.0001f));
    CHECK(near(wrap_angle(-4.0f), -4.0f + 2.0f * glm::pi<float>(), 0.0001f));
    CHECK(wrap_angle(1.0f) == 1.0f);
    CHECK(wrap_angle(glm::pi<float>()) < glm::pi<float>());
    for (const float large : {12345.678f, -98765.4f, 1e6f}) {
        const float wrapped = wrap_angle(large);
        CHECK(wrapped >= -glm::pi<float>());
        CHECK(wrapped < glm::pi<float>());
    }
}

TEST_CASE("speeds and turn rates others see are limited to what the game produces") {
    const game_tuning& tuning = shipped_content().tuning;
    const std::uint8_t walk = mode_number(motion_mode::walk);
    const float walking = tuning.player.walk_speed * tuning.server.motion_speed_scale;
    CHECK(clamp_motion_speed(walk, 1e30f, tuning) == walking);
    CHECK(clamp_motion_speed(walk, -1e30f, tuning) == -walking);
    CHECK(clamp_motion_speed(walk, 1.0f, tuning) == 1.0f);
    CHECK(clamp_turn_rate(tuning.player.turn_rate, tuning) == tuning.player.turn_rate);
    CHECK(clamp_turn_rate(1e30f, tuning) < 100.0f);
}

TEST_CASE("shots out of turn, away from the ball, with unknown clubs, or at impossible times are refused") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(0.0f), 2, 1.0f};
    const glm::vec3 at_ball = beside(ball);
    const float slack = content.tuning.server.wind_time_slack_seconds;
    const auto check = [&](const shot_request& request, const float on_hole = 5.0f) {
        return check_shot(request, ball, at_ball, on_hole, false, content);
    };

    CHECK(check(putt(2)).error == error_wrong_stroke);
    CHECK(check(putt(4)).error == error_wrong_stroke);
    const glm::vec3 far_away(content.tuning.player.ball_interact_radius + content.tuning.server.interact_slack + 1.0f, 0.0f, 0.0f);
    CHECK(check_shot(putt(3), ball, far_away, 5.0f, false, content).error == error_too_far);
    shot_request unknown = putt(3);
    unknown.club_id = "banana";
    CHECK(check(unknown).error == error_unknown_club);

    shot_request early = putt(3);
    early.wind_time = 0.5f;  // before the last shot
    CHECK(check(early).error == error_invalid_shot);
    shot_request stale = putt(3);
    stale.wind_time = 2.0f;  // a calm moment long past
    CHECK(check(stale, 2.0f + slack + 0.01f).error == error_invalid_shot);
    CHECK(check(stale, 2.0f + slack).input.has_value());
    shot_request late = putt(3);
    late.wind_time = 5.0f + slack + 0.01f;
    CHECK(check(late).error == error_invalid_shot);
    late.wind_time = 5.0f + slack;
    CHECK(check(late).input.has_value());

    for (const float bad : {std::nanf(""), INFINITY}) {
        shot_request request = putt(3);
        request.aim_angle = bad;
        CHECK(check(request).error == error_invalid_shot);
        request = putt(3);
        request.power = bad;
        CHECK(check(request).error == error_invalid_shot);
    }
}

TEST_CASE("emotes go by one name each way") {
    for (const emote_id emote : {emote_id::smoke, emote_id::drink}) {
        CHECK(emote_from_name(emote_name(emote)) == emote);
    }
    CHECK(!emote_from_name("dance").has_value());
}
