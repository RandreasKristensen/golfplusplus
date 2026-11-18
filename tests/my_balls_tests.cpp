#include "doctest.h"

#include "core/key_bindings.h"
#include "game/address_play.h"
#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/hub_session.h"
#include "game/interactables.h"
#include "game/motion_sync.h"
#include "game/my_balls.h"
#include "game/online_play.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>

// My balls out: the ball in play and my spares, one running array capped by the
// place's rules.balls_out, the same rules offline and on the server.
namespace {
spare_ball spare(const std::uint64_t id, const std::int64_t placed_at, const glm::vec3& at = glm::vec3(0.0f), const bool in_cup = false) {
    return spare_ball{id, 0, at, in_cup, placed_at};
}

std::vector<std::uint64_t> ids(const std::vector<spare_ball>& spares) {
    std::vector<std::uint64_t> found;
    for (const spare_ball& ball : spares) { found.push_back(ball.id); }
    return found;
}
using id_list = std::vector<std::uint64_t>;

game_state hub_state() {
    game_state state = started_game(fixture_hub_course());
    state.kind = place_kind::hub; state.place.carts = false; state.place.rules = place_rules{false, true, false, true, 10};
    state.hub = hub_definition{};
    state.round.reset(); state.holes.clear(); state.course_holes.clear();
    park_cart(state); strap_bag_on_cart(state);
    game_input bag; bag.move_bag = true;
    update_game(state, bag, 0.016f);
    finish_handling(state);
    return state;
}

game_state course_state() {
    game_state state = started_hole();
    game_input bag; bag.move_bag = true;
    update_game(state, bag, 0.016f);
    finish_handling(state);
    return state;
}

void drop(game_state& state) {
    game_input press; press.drop_practice_ball = true;
    update_game(state, press, 0.016f);
}

// G, then a step to the side, `count` times: balls half a metre apart, all in reach.
void drop_in_a_cluster(game_state& state, const int count) {
    for (int i = 0; i < count; ++i) {
        drop(state);
        state.player.position.x += 0.5f;
    }
}

float hold_seconds(const game_state& state) { return state.tuning.player.ball_pickup_hold_seconds + 0.1f; }

std::size_t balls_out(const game_state& state) { return state.spare_balls.size() + (state.ball ? 1U : 0U); }
}

TEST_CASE("the cap removes the oldest spares first and never the new ball in play") {
    // Three out: the new ball in play and two spares.
    const std::vector<spare_ball> spares{spare(1, 10), spare(2, 20), spare(3, 30)};
    CHECK((ids(spares_after_new_ball(spares, std::nullopt, 3)) == id_list{2, 3}));
    CHECK((ids(spares_after_new_ball(spares, std::nullopt, 4)) == id_list{1, 2, 3}));
    CHECK((ids(spares_after_new_ball(spares, std::nullopt, 10)) == id_list{1, 2, 3}));
    // A course: one ball out, which is the new ball in play.
    CHECK(spares_after_new_ball(spares, std::nullopt, 1).empty());
    CHECK(spares_after_new_ball({}, std::nullopt, 1).empty());
    // Order is by age, not by id or list order.
    CHECK((ids(spares_after_new_ball({spare(5, 30), spare(6, 10), spare(7, 20)}, std::nullopt, 3)) == id_list{7, 5}));
    // The ball I was playing joins them, with its own age: here the oldest, so it is the one to go.
    const std::optional<spare_ball> oldest = spare(0, 5);
    CHECK((ids(spares_after_new_ball(spares, oldest, 4)) == id_list{1, 2, 3}));
    CHECK((ids(spares_after_new_ball(spares, oldest, 3)) == id_list{2, 3}));
    // And where it is the newest, the oldest spare goes instead.
    const std::optional<spare_ball> newest = spare(0, 99);
    CHECK((ids(spares_after_new_ball(spares, newest, 4)) == id_list{2, 3, 0}));
}

TEST_CASE("playing a spare needs it in reach on foot and a ball in play that is at rest") {
    const game_state state = hub_state();
    const game_tuning& tuning = state.tuning;
    const glm::vec3 me(0.0f);
    const spare_ball near_spare = spare(1, 1, glm::vec3(1.0f, 0.0f, 0.0f));
    const std::optional<ball_in_play_state> resting = ball_in_play_state{ball_kind::practice, true};
    CHECK(play_spare_error(resting, true, me, near_spare, tuning).empty());
    CHECK(play_spare_error(std::nullopt, true, me, near_spare, tuning).empty());
    CHECK((play_spare_error(resting, true, me, spare(2, 1, glm::vec3(50.0f, 0.0f, 0.0f)), tuning) == error_too_far));
    CHECK(play_spare_error(resting, false, me, near_spare, tuning) == error_too_far);
    CHECK(play_spare_error(ball_in_play_state{ball_kind::practice, false}, true, me, near_spare, tuning) == error_ball_in_play);
    CHECK(play_spare_error(ball_in_play_state{ball_kind::scored, true}, true, me, near_spare, tuning) == error_ball_in_play);
    CHECK((play_spare_error(resting, true, me, spare(3, 1, glm::vec3(1.0f, 0.0f, 0.0f), true), tuning) == error_ball_in_cup));
}

TEST_CASE("the nearest of my balls in reach is the one to lift; a spare wins a tie, the oldest first") {
    const game_state state = hub_state();
    const game_tuning& tuning = state.tuning;
    const glm::vec3 me(0.0f);
    const glm::vec3 beside(1.0f, 0.0f, 0.0f);
    const glm::vec3 farther(2.0f, 0.0f, 0.0f);
    CHECK(!nearest_ball_to_lift(std::nullopt, {}, true, me, tuning));
    CHECK((!nearest_ball_to_lift(std::nullopt, {spare(1, 1, glm::vec3(50.0f, 0.0f, 0.0f))}, true, me, tuning)));
    CHECK((!nearest_ball_to_lift(std::nullopt, {spare(1, 1, beside)}, false, me, tuning)));
    auto choice = nearest_ball_to_lift(farther, {spare(1, 1, beside)}, true, me, tuning);
    REQUIRE(choice); CHECK(!choice->in_play); CHECK(choice->spare_id == 1);
    choice = nearest_ball_to_lift(beside, {spare(1, 1, farther)}, true, me, tuning);
    REQUIRE(choice); CHECK(choice->in_play);
    choice = nearest_ball_to_lift(beside, {spare(4, 9, beside), spare(3, 2, beside)}, true, me, tuning);
    REQUIRE(choice); CHECK(!choice->in_play); CHECK(choice->spare_id == 3);
    // A ball in the hub's cup lifts like any other.
    choice = nearest_ball_to_lift(std::nullopt, {spare(1, 1, beside, true)}, true, me, tuning);
    REQUIRE(choice); CHECK(choice->spare_id == 1);
}

TEST_CASE("a struck ball meets my spares at rest, but not one lying where it starts nor one in a cup") {
    const game_state state = hub_state();
    const glm::vec3 from(0.0f);
    const auto entries = spares_in_snapshot({spare(1, 1, glm::vec3(3.0f, 0.0f, 0.0f)), spare(2, 2, from),
        spare(3, 3, glm::vec3(5.0f, 0.0f, 0.0f), true)}, from, state.tuning);
    REQUIRE(entries.size() == 1U);
    CHECK(entries.front().kind == snapshot_kind::ball);
    CHECK((entries.front().position == glm::vec3(3.0f, 0.0f, 0.0f)));
    CHECK(!entries.front().shooter);
}

TEST_CASE("every place states how many balls a player may have out: ten in the hub, one on a course") {
    CHECK(fixture_hub_course().rules.balls_out == 10);
    CHECK(shipped_content().hub.rules.balls_out == 10);
    for (const course_definition& course : shipped_content().courses) {
        CHECK(course.rules.balls_out == 1);
    }
}

TEST_CASE("offline G drops a ball and keeps the one I was playing where it lies") {
    game_state state = hub_state();
    const glm::vec3 first = state.player.position;
    drop(state);
    REQUIRE(state.ball);
    CHECK(state.spare_balls.empty());
    state.player.position.x += 5.0f;
    drop(state);
    REQUIRE(state.ball);
    REQUIRE(state.spare_balls.size() == 1U);
    CHECK(near(state.spare_balls.front().position.x, first.x, 0.01f));
    CHECK(near(state.ball->position.x, first.x + 5.0f, 0.01f));
    CHECK(state.mode == game_mode::walking);
    // A ball still flying is not at rest: it is thrown away, as before.
    play_shot(state, shot_result{state.ball->position + glm::vec3(50, 0, 0), false, 3.0f,
        {state.ball->position, state.ball->position + glm::vec3(50, 0, 0)}, {}});
    REQUIRE(state.shot);
    drop(state);
    CHECK(!shot_playing(state));
    CHECK(state.spare_balls.size() == 1U);
}

TEST_CASE("offline every ball out is one array of ten: the eleventh removes the oldest, never the ball in play") {
    game_state state = hub_state();
    std::vector<float> placed;
    for (int i = 0; i < 12; ++i) {
        placed.push_back(state.player.position.x);
        drop(state);
        state.player.position.x += 5.0f;
        CHECK(balls_out(state) == static_cast<std::size_t>(std::min(i + 1, 10)));
    }
    REQUIRE(state.ball);
    CHECK(near(state.ball->position.x, placed.back(), 0.01f));
    REQUIRE(state.spare_balls.size() == 9U);
    // The first two drops are gone; the oldest left is the third.
    CHECK(near(state.spare_balls.front().position.x, placed[2], 0.01f));
    CHECK(near(state.spare_balls.back().position.x, placed[10], 0.01f));
    for (std::size_t i = 1; i < state.spare_balls.size(); ++i) {
        CHECK(state.spare_balls[i - 1].placed_at < state.spare_balls[i].placed_at);
    }
}

TEST_CASE("teeing at a bay leaves the earlier balls where they lie, up to the cap") {
    game_state state = hub_state();
    state.hub->tee_mats.push_back({state.world.hole_starts[0].position, state.world.hole_starts[0].position + glm::vec3(0, 0, 100), 3.0f});
    state.hub->tee_mats.push_back({state.world.hole_starts[0].position + glm::vec3(5, 0, 0),
        state.world.hole_starts[0].position + glm::vec3(5, 0, 100), 3.0f});
    REQUIRE(tee_up_hub(state, 0));
    const glm::vec3 first = state.ball->position;
    REQUIRE(tee_up_hub(state, 1));
    REQUIRE(state.spare_balls.size() == 1U);
    CHECK(state.spare_balls.front().position == first);
    CHECK(state.ball->tee_mat == std::optional<std::size_t>(1));
    // The same bay again tees a fresh ball, and the one on it stays beside.
    REQUIRE(tee_up_hub(state, 1));
    CHECK(state.spare_balls.size() == 2U);
    // Leaving the hub by the bus puts every ball away.
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(state.spare_balls.empty());
    CHECK(!state.ball);
}

TEST_CASE("the same keys on the hub and on a course: G three times, then F at a spare swaps it into play") {
    game_state hub = hub_state();
    drop_in_a_cluster(hub, 3);
    REQUIRE(hub.ball); REQUIRE(hub.spare_balls.size() == 2U);
    // Stand by the first drop: the nearest ball is the oldest spare.
    const glm::vec3 oldest = hub.spare_balls.front().position;
    const glm::vec3 played_before = hub.ball->position;
    hub.player.position = oldest;
    const auto near_spare = nearest_interactable(hub);
    REQUIRE(near_spare); CHECK(near_spare->kind == interactable_kind::spare_ball);
    tap_interact(hub);
    CHECK(hub.mode == game_mode::addressing);
    REQUIRE(hub.ball);
    CHECK(near(hub.ball->position, oldest, 0.001f));
    CHECK(hub.ball->strokes == 0);
    REQUIRE(hub.spare_balls.size() == 2U);
    CHECK(std::any_of(hub.spare_balls.begin(), hub.spare_balls.end(), [&](const spare_ball& other) {
        return near(other.position, played_before, 0.001f); }));
    CHECK(!std::any_of(hub.spare_balls.begin(), hub.spare_balls.end(), [&](const spare_ball& other) {
        return near(other.position, oldest, 0.001f); }));
    CHECK(balls_out(hub) == 3U);

    // A course has one ball out and no drops: G does nothing, and F still addresses my ball.
    game_state course = course_state();
    REQUIRE(course.ball);
    const glm::vec3 teed = course.ball->position;
    drop_in_a_cluster(course, 3);
    CHECK(course.spare_balls.empty());
    CHECK(course.ball->position == teed);
    CHECK(course.notice.has_value());
    course.player.position = teed;
    tap_interact(course);
    CHECK(course.mode == game_mode::addressing);
}

TEST_CASE("F held clears a cluster of my balls one by one, the nearest first") {
    game_state state = hub_state();
    drop_in_a_cluster(state, 4);
    REQUIRE(balls_out(state) == 4U);
    state.player.position = state.spare_balls.front().position;
    game_input press = interact_input();
    press.interact_held = true;
    update_game(state, press, 0.016f);
    game_input held; held.interact_held = true;
    std::size_t lifted = 0;
    std::size_t before = balls_out(state);
    for (int frame = 0; frame < 400 && balls_out(state) > 0; ++frame) {
        update_game(state, held, 0.02f);
        if (balls_out(state) < before) { ++lifted; CHECK(balls_out(state) == before - 1); before = balls_out(state); }
    }
    CHECK(lifted == 4U);
    CHECK(!state.ball);
    CHECK(state.spare_balls.empty());
    // Each lift takes a hold time of its own: 2.5 of them lift two of three balls.
    game_state timed = hub_state();
    drop_in_a_cluster(timed, 3);
    timed.player.position = timed.spare_balls.front().position;
    hold_interact(timed, timed.tuning.player.ball_pickup_hold_seconds * 2.5f);
    CHECK(balls_out(timed) == 1U);
}

TEST_CASE("a course ball is still lifted only from its cup, and nothing there is a spare") {
    game_state course = course_state();
    hold_interact(course, hold_seconds(course));
    CHECK(course.ball.has_value());
    course.mode = game_mode::walking; course.address = address_state{};
    course.ball->place = ball_place::in_cup;
    course.player.position = course.ball->position;
    hold_interact(course, hold_seconds(course));
    CHECK(!course.ball.has_value());
    CHECK(course.spare_balls.empty());
}

TEST_CASE("a ball I strike meets my spare lying on its line") {
    game_state state = course_state();
    while (state.clubs[state.selected_club].label != "P") { state.selected_club += 1; }
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 forward = yaw_direction(state.player.yaw);
    state.spare_balls.push_back(spare_ball{1, 0, resting_ball_position(state.area, ball + forward * 3.0f,
        state.tuning.scale.ball_physics_radius_meters), false, 1});
    address_looking_down(state);
    REQUIRE(state.mode == game_mode::addressing);
    state.address.setup.aim_angle = state.player.yaw;
    state.address.setup.face_open = 0.0f;
    state.address.setup.backswing = 0.8f;
    start_swing_on_time(state);
    swing_through(state);
    REQUIRE(state.shot.has_value());
    const auto& events = state.shot->result.events;
    CHECK(std::any_of(events.begin(), events.end(), [](const shot_event& event) { return event.kind == shot_event_kind::ball_hit; }));
}

TEST_CASE("online a drop, a swap and a lift each wait for the server's rows, and a refusal leaves the rows as they are") {
    game_state state = hub_state();
    state.play = play_mode::online;
    state.online.account_id = 1; state.online.room_ready = true;
    state.online.room = online_room{7, online_hub_course, 1, 1, place_kind::hub};
    state.online.players[1].motion = current_motion(state);
    const float radius = state.tuning.scale.ball_physics_radius_meters;
    const glm::vec3 here = resting_ball_position(state.area, state.player.position, radius);
    const auto mine = [&](const std::uint64_t id, const std::int64_t at, const glm::vec3& position, const std::uint64_t account = 1) {
        state.online.spare_balls[id] = spare_ball{id, account, position, false, at}; ++state.online.ball_updates;
    };

    // Drop: nothing predicted, the answer is the ball row and the spare the old ball became.
    state.online.balls[1] = {-1, here + glm::vec3(4, 0, 0), 2, false, ball_kind::practice};
    state.ball = my_ball{std::nullopt, here + glm::vec3(4, 0, 0), 2, ball_place::at_rest, ball_kind::practice};
    request_practice_drop(state);
    REQUIRE(state.hub_flow.ball_request == net_command_type::place_practice_ball);
    REQUIRE(state.net_commands.size() >= 1U);
    CHECK(state.ball->strokes == 2);
    update_online_play(state, 0);
    CHECK(state.hub_flow.ball_request);
    state.online.balls[1] = {-1, here, 0, false, ball_kind::practice};
    mine(10, 1, here + glm::vec3(4, 0, 0));
    update_online_play(state, 0);
    CHECK(!state.hub_flow.ball_request);
    REQUIRE(state.ball); CHECK(near(state.ball->position, here, 0.001f)); CHECK(state.ball->strokes == 0);
    REQUIRE(my_spare_balls(state).size() == 1U);

    // Another golfer's spare is theirs: not mine to play or lift, but in the room's rows.
    mine(30, 5, here, 2);
    CHECK(my_spare_balls(state).size() == 1U);
    CHECK(remote_balls(state).size() >= 2U);

    // Swap: asks, waits, and a refusal puts my ball back as its row has it.
    state.net_commands.clear();
    state.player.position = here + glm::vec3(4, 0, 0);
    REQUIRE(play_spare_ball(state, 10));
    REQUIRE(state.hub_flow.ball_request == net_command_type::play_ball);
    CHECK(state.hub_flow.ball_target == 10U);
    REQUIRE(!state.net_commands.empty());
    CHECK(state.net_commands.back().type == net_command_type::play_ball);
    CHECK(state.net_commands.back().ball_id == 10U);
    CHECK(near(state.ball->position, here, 0.001f));
    CHECK(!play_spare_ball(state, 10));  // one request at a time
    state.online.refusals.push_back({reducer_play_ball, error_too_far});
    update_online_play(state, 0);
    CHECK(!state.hub_flow.ball_request);
    CHECK(near(state.ball->position, here, 0.001f));
    CHECK(my_spare_balls(state).size() == 1U);
    CHECK(state.notice.has_value());

    // The server swaps in one transaction: the spare is gone, my ball row is its place, mine the old ball's.
    REQUIRE(play_spare_ball(state, 10));
    ++state.online.ball_updates;
    update_online_play(state, 0);
    CHECK(state.hub_flow.ball_request);  // the rows are still the old ones
    state.online.spare_balls.erase(10);
    state.online.balls[1] = {-1, here + glm::vec3(4, 0, 0), 0, false, ball_kind::practice};
    mine(11, 2, here);
    update_online_play(state, 0);
    CHECK(!state.hub_flow.ball_request);
    REQUIRE(state.ball); CHECK(near(state.ball->position, here + glm::vec3(4, 0, 0), 0.001f));
    CHECK(state.mode == game_mode::addressing);
    CHECK((ids(my_spare_balls(state)) == id_list{11}));

    // Lifting a spare waits until it is gone from the rows; the ball in play stays.
    state.mode = game_mode::walking; state.address = address_state{};
    state.player.position = here;
    REQUIRE(pick_up_spare_ball(state, 11));
    CHECK(state.net_commands.back().type == net_command_type::pick_up_ball);
    CHECK(state.net_commands.back().ball_id == 11U);
    ++state.online.ball_updates;
    update_online_play(state, 0);
    CHECK(state.hub_flow.ball_request);
    state.online.spare_balls.erase(11); ++state.online.ball_updates;
    update_online_play(state, 0);
    CHECK(!state.hub_flow.ball_request);
    CHECK(state.ball);
    CHECK(my_spare_balls(state).empty());

}
