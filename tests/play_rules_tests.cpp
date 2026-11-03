#include "doctest.h"

#include "account_rules.h"
#include "game/net_types.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"
#include "play_rules.h"
#include "game/server_errors.h"

#include "server_test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
std::uint8_t mode_number(const motion_mode mode) {
    return static_cast<std::uint8_t>(mode);
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
    request.backswing = 0.5f;
    request.shot_at = 3000;
    return request;
}

// A golfer with the putter in hand and their bag set down, every flag in.
server_golfer putting() {
    server_golfer golfer;
    golfer.club_in_hand = "putter";
    return golfer;
}
}

namespace {
// How fast the shipped game moves a player in the default cart, with no items running.
movement_limits shipped_limits() {
    const game_content& content = shipped_content();
    return movement_limits_for(content.tuning, *content.items.find(content.default_loadout.cart)->cart, {}, 0);
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
    const movement_limits limits = shipped_limits();
    const std::uint8_t walk = mode_number(motion_mode::walk);
    const std::uint8_t cart = mode_number(motion_mode::cart);
    const float slack = tuning.server.motion_distance_slack;
    // On foot the fastest is running.
    const float walking = tuning.player.walk_speed * tuning.player.run_factor * tuning.server.motion_speed_scale;
    const float driving = limits.cart_speed * tuning.server.motion_speed_scale;
    const glm::vec3 start(0.0f);

    CHECK(check_motion(was(start, walk), glm::vec3(walking * 2.0f + slack - 0.01f, 50.0f, 0.0f), 2.0f, walk, slack, limits, tuning).allowed);
    CHECK(!check_motion(was(start, walk), glm::vec3(walking * 2.0f + slack + 0.01f, 0.0f, 0.0f), 2.0f, walk, slack, limits, tuning).allowed);
    // Getting into the cart allows cart speed from the start.
    CHECK(check_motion(was(start, walk), glm::vec3(driving * 2.0f, 0.0f, 0.0f), 2.0f, cart, slack, limits, tuning).allowed);
    CHECK(!check_motion(was(start, walk), glm::vec3(0.0f, 0.0f, slack + 0.01f), -5.0f, walk, slack, limits, tuning).allowed);

    // Quick calls each a little too far use the slack up instead of adding it.
    float slack_left = slack;
    int accepted = 0;
    glm::vec3 at = start;
    for (int i = 0; i < 100; ++i) {
        const glm::vec3 to = at + glm::vec3(walking * 0.01f + 1.0f, 0.0f, 0.0f);
        const motion_check check = check_motion(was(at, walk), to, 0.01f, walk, slack_left, limits, tuning);
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
    const movement_limits limits = shipped_limits();
    const std::uint8_t idle = mode_number(motion_mode::idle);
    const std::uint8_t cart = mode_number(motion_mode::cart);
    const float slack = tuning.server.motion_distance_slack;
    const float gap = tuning.server.max_motion_gap_seconds;
    // From standing to driving, the faster of the two counts.
    const float fastest = std::max(limits.foot_speed, limits.cart_speed);
    const float reach = fastest * tuning.server.motion_speed_scale * gap + slack;

    CHECK(check_motion(was(glm::vec3(0.0f), idle), glm::vec3(reach - 0.01f, 0.0f, 0.0f), 30.0f, cart, slack, limits, tuning).allowed);
    CHECK(!check_motion(was(glm::vec3(0.0f), idle), glm::vec3(reach + 0.01f, 0.0f, 0.0f), 30.0f, cart, slack, limits, tuning).allowed);
}

TEST_CASE("interaction reach is the radius plus slack, horizontally") {
    const server_tuning& tuning = shipped_content().tuning.server;
    const glm::vec3 target(10.0f, 0.0f, 0.0f);
    CHECK(within_interact_reach(glm::vec3(10.0f + 3.0f + tuning.interact_slack, 40.0f, 0.0f), target, 3.0f, tuning));
    CHECK(!within_interact_reach(glm::vec3(10.0f + 3.0f + tuning.interact_slack + 0.01f, 0.0f, 0.0f), target, 3.0f, tuning));
}

TEST_CASE("a seat is in reach within the cart's board reach plus slack") {
    const game_tuning& tuning = shipped_content().tuning;
    const movement_limits limits = shipped_limits();
    const cart_pose cart{glm::vec3(10.0f, 0.0f, 0.0f), 0.0f};
    const glm::vec3 seat = seat_position(cart, cart_seat::passenger, tuning.cart);
    const glm::vec3 away = -yaw_left(yaw_direction(cart.yaw));
    const float reach = tuning.cart.board_reach + tuning.server.interact_slack;
    CHECK(within_seat_reach(cart, cart_seat::passenger, seat + away * (reach - 0.01f), tuning));
    CHECK(!within_seat_reach(cart, cart_seat::passenger, seat + away * (reach + 0.01f), tuning));
}

TEST_CASE("a joining player's cart parks in the first free slot beside hole 1") {
    const cart_tuning& tuning = shipped_content().tuning.cart;
    const glm::vec3 start(0.0f, 0.0f, 0.0f);
    const glm::vec3 pin(0.0f, 0.0f, 100.0f);
    const auto spot = [&](const std::size_t slot) { return parked_cart_pose(start, pin, slot, tuning).position; };
    CHECK(free_parking_slot({}, start, pin, tuning) == 0U);
    CHECK(free_parking_slot({spot(0)}, start, pin, tuning) == 1U);
    CHECK(free_parking_slot({spot(0), spot(2)}, start, pin, tuning) == 1U);
    CHECK(free_parking_slot({spot(1), spot(0)}, start, pin, tuning) == 2U);
    // A cart driven away frees its slot.
    CHECK(free_parking_slot({spot(1) + glm::vec3(40.0f, 0.0f, 0.0f)}, start, pin, tuning) == 0U);
    // Slots stand side by side, a park_spacing apart, to the right of the hole.
    CHECK(near(horizontal_distance(spot(0), spot(1)), tuning.park_spacing, 0.0001f));
    CHECK(glm::dot(spot(0) - start, yaw_left(glm::vec3(0.0f, 0.0f, 1.0f))) < 0.0f);
}

TEST_CASE("a valid shot is the client's setup from the server's ball in the golfer's hand, its dials within their limits") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(1.0f, 2.0f, 3.0f), 2, 1000};
    const club_definition& putter = *find_club(content.clubs, "putter");
    const setup_limits limits = setup_limits_for(putter, content.tuning.setup);
    shot_request request = putt(3);
    request.backswing = limits.backswing_max;
    request.face_open = limits.face_open_max * 0.5f;
    request.ball_position = 0.4f;
    server_golfer lefty = putting();
    lefty.golfer.hand = handedness::left;

    const shot_check full = check_shot(request, ball, beside(ball), lefty, 5000, content);
    REQUIRE(full.input.has_value());
    CHECK(full.error.empty());
    CHECK(full.input->ball_start == ball.position);
    CHECK(full.input->backswing == limits.backswing_max);
    CHECK(full.input->club_id == "putter");
    CHECK(full.input->shot_at == request.shot_at);
    CHECK(full.input->face_open == request.face_open);
    CHECK(full.input->ball_position == request.ball_position);
    CHECK(full.input->hand == handedness::left);

    // Past any dial's limit it is refused, not held.
    using past_limit = void (*)(shot_request&, const setup_limits&);
    for (const past_limit past : {past_limit{[](shot_request& r, const setup_limits& l) { r.backswing = l.backswing_max + 0.5f; }},
                                  past_limit{[](shot_request& r, const setup_limits&) { r.backswing = 0.0f; }},
                                  past_limit{[](shot_request& r, const setup_limits& l) { r.face_open = l.face_open_max + 0.1f; }},
                                  past_limit{[](shot_request& r, const setup_limits& l) { r.face_open = l.face_open_min - 0.1f; }},
                                  past_limit{[](shot_request& r, const setup_limits& l) { r.ball_position = l.ball_position_max + 0.1f; }}}) {
        shot_request bad = putt(3);
        past(bad, limits);
        CHECK(check_shot(bad, ball, beside(ball), putting(), 5000, content).error == error_invalid_shot);
    }
}

TEST_CASE("a shot with another club than the one in hand, the bag on the back, or the flags not as the room has them is refused") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(0.0f), 0, 0};
    server_golfer golfer = putting();
    golfer.club_in_hand = "driver";
    CHECK(check_shot(putt(1), ball, beside(ball), golfer, 3000, content).error == error_club_not_in_hand);
    golfer = putting();
    golfer.bag_on_back = true;
    CHECK(check_shot(putt(1), ball, beside(ball), golfer, 3000, content).error == error_bag_on_back);

    // The flag on hole 3 is out on the room's row; this client still has it in.
    golfer = putting();
    golfer.flags_out = {2};
    CHECK(check_shot(putt(1), ball, beside(ball), golfer, 3000, content).error == error_flag_changed);
    shot_request agreed = putt(1);
    agreed.flags_out = {2};
    CHECK(check_shot(agreed, ball, beside(ball), golfer, 3000, content).input.has_value());
}

TEST_CASE("the server picks each shot's dice: other times, players and strokes roll others") {
    const std::string secret(min_server_secret_length, 's');
    const std::uint64_t seed = server_shot_seed(secret, 1000000, 7, 1);
    CHECK(seed == server_shot_seed(secret, 1000000, 7, 1));
    CHECK(seed != server_shot_seed(secret, 1000001, 7, 1));
    CHECK(seed != server_shot_seed(secret, 1000000, 8, 1));
    CHECK(seed != server_shot_seed(secret, 1000000, 7, 2));
    // Knowing the clock, the account and the stroke is not enough.
    CHECK(seed != server_shot_seed(std::string(min_server_secret_length, 't'), 1000000, 7, 1));
}

TEST_CASE("a bag moves only within reach: off a cart or the ground onto the back, from the back onto a free cart spot or down") {
    const server_content content = fixture_server_content();
    const game_tuning& tuning = content.tuning;
    const std::optional<cart_pose> cart = cart_pose{glm::vec3(0.0f), 0.0f};
    const std::optional<cart_pose> none;
    const glm::vec3 spot = bag_spot(*cart, cart_seat::driver, tuning.cart);
    const bag_place on_cart{bag_where::on_cart, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f};
    const bag_place on_back{bag_where::on_back, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f};
    const float far = tuning.player.bag_reach + tuning.server.interact_slack + 1.0f;
    CHECK(check_bag_move(on_cart, on_back, spot + glm::vec3(0.5f, 0.0f, 0.0f), cart, none, false, tuning).empty());
    CHECK(check_bag_move(on_cart, on_back, spot + glm::vec3(far, 0.0f, 0.0f), cart, none, false, tuning) == error_bag_out_of_reach);

    const glm::vec3 me(20.0f, 0.0f, 0.0f);
    const bag_place down{bag_where::on_ground, cart_seat::driver, 0, me + glm::vec3(0.7f, 0.0f, 0.0f), 0.0f};
    CHECK(check_bag_move(on_back, down, me, none, none, false, tuning).empty());
    const bag_place thrown{bag_where::on_ground, cart_seat::driver, 0, me + glm::vec3(far, 0.0f, 0.0f), 0.0f};
    CHECK(check_bag_move(on_back, thrown, me, none, none, false, tuning) == error_bag_out_of_reach);
    CHECK(check_bag_move(on_back, on_cart, me, none, cart, false, tuning) == error_bag_out_of_reach);
    CHECK(check_bag_move(down, on_cart, me, none, cart, false, tuning) == error_invalid_motion);

    // Onto another's cart behind a free seat; never where another bag is.
    const bag_place on_theirs{bag_where::on_cart, cart_seat::passenger, 7, glm::vec3(0.0f), 0.0f};
    const glm::vec3 their_spot = bag_spot(*cart, cart_seat::passenger, tuning.cart);
    CHECK(check_bag_move(on_back, on_theirs, their_spot, none, cart, false, tuning).empty());
    CHECK(check_bag_move(on_back, on_theirs, their_spot, none, cart, true, tuning) == error_bag_spot_taken);

    // Clubs come from a bag set down or on its cart, within reach.
    CHECK(check_take_club("driver", down, me, none, content).empty());
    CHECK(check_take_club("driver", on_back, me, none, content) == error_bag_on_back);
    CHECK(check_take_club("driver", on_cart, me, cart, content) == error_bag_out_of_reach);
    CHECK(check_take_club("banana", down, me, none, content) == error_unknown_club);

    // With no cart there, the bag goes down and back up, never onto one.
    const glm::vec3 at_origin = spot;
    CHECK(check_bag_move(on_back, on_cart, at_origin, none, none, false, tuning) == error_bag_out_of_reach);
    CHECK(check_bag_move(on_cart, on_back, at_origin, none, none, false, tuning) == error_bag_out_of_reach);
    CHECK(check_bag_move(on_back, down, me, none, none, false, tuning).empty());
    CHECK(check_bag_move(down, on_back, me, none, none, false, tuning).empty());
    CHECK(check_take_club("driver", on_cart, at_origin, none, content) == error_bag_out_of_reach);
    CHECK(check_take_club("driver", down, me, none, content).empty());
}

TEST_CASE("a driver's ride motion reaches only as far as their cart could have gone since their last motion") {
    const server_content content = fixture_server_content();
    const movement_limits limits{1.0f, 5.0f, 1.0f};
    const server_tuning& server = content.tuning.server;
    const glm::vec3 last(0.0f);
    const float reach = limits.cart_speed * server.motion_speed_scale * 1.0f + server.motion_distance_slack;
    CHECK(ride_motion_plausible(last, 1.0f, glm::vec3(reach - 0.1f, 0.0f, 0.0f), limits, server));
    CHECK(!ride_motion_plausible(last, 1.0f, glm::vec3(reach + 0.1f, 0.0f, 0.0f), limits, server));
    // A long gap counts only up to max_motion_gap_seconds.
    const float longest = limits.cart_speed * server.motion_speed_scale * server.max_motion_gap_seconds + server.motion_distance_slack;
    CHECK(!ride_motion_plausible(last, 100.0f, glm::vec3(longest + 0.1f, 0.0f, 0.0f), limits, server));
}

TEST_CASE("aim must come wrapped into one turn") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(0.0f), 0, 0};
    shot_request request = putt(1);
    for (const float aim : {-3.14159f, 0.0f, 3.14159f}) {
        request.aim_angle = aim;
        CHECK(check_shot(request, ball, beside(ball), putting(), 3000, content).input.has_value());
    }
    request.aim_angle = 7.0f;
    CHECK(check_shot(request, ball, beside(ball), putting(), 3000, content).error == error_invalid_shot);

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
    const movement_limits limits = shipped_limits();
    const std::uint8_t walk = mode_number(motion_mode::walk);
    const float walking = limits.foot_speed * tuning.server.motion_speed_scale;
    CHECK(near(limits.foot_speed, tuning.player.walk_speed * tuning.player.run_factor, 0.0001f));
    CHECK(clamp_motion_speed(walk, 1e30f, limits, tuning.server) == walking);
    CHECK(clamp_motion_speed(walk, -1e30f, limits, tuning.server) == -walking);
    CHECK(clamp_motion_speed(walk, 1.0f, limits, tuning.server) == 1.0f);
    CHECK(clamp_turn_rate(tuning.player.turn_rate, limits) == tuning.player.turn_rate);
    CHECK(clamp_turn_rate(1e30f, limits) < 100.0f);
}

TEST_CASE("shots out of turn, away from the ball, with unknown clubs, or at impossible times are refused") {
    const server_content content = fixture_server_content();
    const server_ball ball{glm::vec3(0.0f), 2, 1000};
    const glm::vec3 at_ball = beside(ball);
    const std::int64_t slack = static_cast<std::int64_t>(content.tuning.server.shot_time_slack_seconds * 1000.0f);
    const auto check = [&](const shot_request& request, const std::int64_t now = 5000) {
        return check_shot(request, ball, at_ball, putting(), now, content);
    };

    CHECK(check(putt(2)).error == error_wrong_stroke);
    CHECK(check(putt(4)).error == error_wrong_stroke);
    const glm::vec3 far_away(content.tuning.player.ball_interact_radius + content.tuning.server.interact_slack + 1.0f, 0.0f, 0.0f);
    CHECK(check_shot(putt(3), ball, far_away, putting(), 5000, content).error == error_too_far);
    shot_request unknown = putt(3);
    unknown.club_id = "banana";
    CHECK(check(unknown).error == error_unknown_club);

    shot_request early = putt(3);
    early.shot_at = 500;  // before the last shot
    CHECK(check(early, 1000).error == error_invalid_shot);
    shot_request stale = putt(3);
    stale.shot_at = 2000;  // a calm moment long past
    CHECK(check(stale, 2000 + slack + 1).error == error_invalid_shot);
    CHECK(check(stale, 2000 + slack).input.has_value());
    shot_request late = putt(3);
    late.shot_at = 5000 + slack + 1;
    CHECK(check(late).error == error_invalid_shot);
    late.shot_at = 5000 + slack;
    CHECK(check(late).input.has_value());

    for (const float bad : {std::nanf(""), INFINITY}) {
        shot_request request = putt(3);
        request.aim_angle = bad;
        CHECK(check(request).error == error_invalid_shot);
        request = putt(3);
        request.backswing = bad;
        CHECK(check(request).error == error_invalid_shot);
    }
}

TEST_CASE("an item is used only as equipped, and not again while its use plays") {
    const server_content content = fixture_server_content();
    const loadout& equipped = content.default_loadout;
    const item_definition& cigarette = *content.items.find(equipped.smokable);
    const std::int64_t use_ms = static_cast<std::int64_t>(cigarette.use->seconds_per_use * 1000.0f);
    const std::int64_t slack_ms = static_cast<std::int64_t>(content.tuning.server.timing_slack_seconds * 1000.0f);
    CHECK(check_item_use(equipped.smokable, equipped, 0, 5000, content).empty());
    CHECK(check_item_use(equipped.drinkable, equipped, 0, 5000, content).empty());
    // Still playing; a little before the end is the client's frames.
    CHECK(check_item_use(equipped.smokable, equipped, 5000, 5000 + use_ms / 2, content) == error_too_soon);
    CHECK(check_item_use(equipped.smokable, equipped, 5000, 5000 + use_ms - slack_ms / 2, content).empty());
    // Only what is equipped, and only what has a use.
    loadout other = equipped;
    other.smokable = "premium_cigarette";
    CHECK(check_item_use(equipped.smokable, other, 0, 5000, content) == error_not_equipped);
    CHECK(check_item_use("no_such_item", equipped, 0, 5000, content) == error_unknown_item);
    CHECK(check_item_use(equipped.cart, equipped, 0, 5000, content) == error_unknown_item);
}

TEST_CASE("items that speed a player up let the server's motion check allow it") {
    const server_content content = fixture_server_content();
    item_effect faster;
    faster.target = effect_target_walk_speed;
    faster.bucket = effect_bucket::increased;
    faster.value = 0.5f;
    faster.duration = effect_duration::seconds;
    faster.seconds = 10.0f;
    const std::vector<active_effect> effects{active_effect{"shoes", faster, 0, 10000}};
    const cart_spec& cart = *content.items.find(content.default_loadout.cart)->cart;
    const float running = content.tuning.player.walk_speed * content.tuning.player.run_factor;
    CHECK(near(movement_limits_for(content.tuning, cart, effects, 5000).foot_speed, running * 1.5f, 0.0001f));
    CHECK(near(movement_limits_for(content.tuning, cart, effects, 20000).foot_speed, running, 0.0001f));
}

TEST_CASE("a player's cart sets how fast and how sharply the server lets them drive") {
    const game_content& content = shipped_content();
    cart_spec cart = *content.items.find(content.default_loadout.cart)->cart;
    const movement_limits stock = movement_limits_for(content.tuning, cart, {}, 0);
    CHECK(near(stock.cart_speed, game_top_speed(cart, content.tuning.cart) / content.tuning.scale.meters_per_world_unit, 0.0001f));
    cart.top_speed_mps *= 2.0f;
    const movement_limits faster = movement_limits_for(content.tuning, cart, {}, 0);
    CHECK(near(faster.cart_speed, 2.0f * stock.cart_speed, 0.0001f));
    CHECK(faster.turn_rate >= stock.turn_rate);
    const std::uint8_t driving = mode_number(motion_mode::cart);
    CHECK(clamp_motion_speed(driving, 1e30f, stock, content.tuning.server) ==
          stock.cart_speed * content.tuning.server.motion_speed_scale);
}
