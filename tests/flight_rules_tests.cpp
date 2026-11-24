#include "doctest.h"

#include "flight_codec.h"
#include "flight_rules.h"
#include "game/ball_contact.h"
#include "game/server_errors.h"
#include "game/step_timeline.h"
#include "movement_rules.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <cstring>
#include <optional>
#include <vector>

namespace {
constexpr std::int64_t launch_ms = 5'000'000;
constexpr std::uint64_t shooter_account = 3;

bool same_bits(const float a, const float b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

bool same_bits(const glm::vec3& a, const glm::vec3& b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z);
}

// A ball flying straight along +X in still air, `height` over the ground,
// and where a golfer standing in its way, 10 m on, has their feet.
struct lane {
    shot_course course;
    ball_state ball;
    glm::vec3 feet{0.0f};
    shot_record record;
};

lane flat_lane(game_state& state, const float speed_mps, const float height) {
    still_air(state);
    lane made{current_shot_course(state), still_ball(state), glm::vec3(0.0f), shot_record{}};
    made.feet = resting_on_terrain(state, state.ball->position + glm::vec3(10.0f, 0.0f, 0.0f)) -
        glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f);
    made.ball.position = made.feet + glm::vec3(-10.0f, height, 0.0f);
    made.ball.velocity = glm::vec3(speed_mps / state.tuning.scale.meters_per_world_unit, 0.0f, 0.0f);
    made.record.setup.shot_at = launch_ms;
    made.record.seed = 99;
    return made;
}

// The flight of the lane's ball with nothing met, as the server keeps it.
live_flight lane_flight(const lane& l, const game_tuning& tuning) {
    live_flight live;
    live.record = l.record;
    live.stroke = 1;
    live.flight = fly_ball(l.ball, launch_ms, struck_course(l.record, l.course, tuning), tuning);
    return live;
}

snapshot_entry golfer_at(const glm::vec3& feet, const std::uint64_t account = 7) {
    return snapshot_entry{snapshot_kind::person, feet, 0.0f, false, account};
}

mover_path moving_path(const snapshot_entry& who, const glm::vec3& from, const glm::vec3& velocity_mps, const std::int64_t from_ms,
                       const int steps, const game_tuning& tuning) {
    mover_path path;
    path.mover = who;
    for (int i = 0; i <= steps; ++i) {
        const float seconds = static_cast<float>(i) * move_step_seconds;
        path.samples.push_back(mover_sample{step_time_ms(from_ms, static_cast<std::uint64_t>(i)),
                                            from + velocity_mps / tuning.scale.meters_per_world_unit * seconds, who.yaw});
    }
    return path;
}

bool same_result(const shot_result& a, const shot_result& b) {
    if (a.trajectory.size() != b.trajectory.size() || a.events.size() != b.events.size() ||
        a.person_hits.size() != b.person_hits.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.trajectory.size(); ++i) {
        if (!same_bits(a.trajectory[i], b.trajectory[i])) {
            return false;
        }
    }
    return same_bits(a.rest_position, b.rest_position) && same_bits(a.duration, b.duration) && a.holed == b.holed &&
        a.penalty_strokes == b.penalty_strokes;
}

shot_course with_solid(const shot_course& course, const solid& thing) {
    shot_course met = course;
    met.dynamic.push_back(thing);
    return met;
}

constexpr std::int64_t forever = launch_ms + 600'000;
}

TEST_CASE("a flight and a live flight encode to bytes and decode to the same to the bit, and resume the same") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const flight_env env{l.course, tuning, state.clubs};
    live_flight live = lane_flight(l, tuning);
    live.golfer.levels["driving"] = 12;
    live.golfer.spread_scale = 0.5f;
    live.golfer.club_speed_scale = 1.05f;
    live.flags_out = {2, 4};
    live.record.setup.club_id = "P";
    live.record.snapshot.push_back(snapshot_entry{snapshot_kind::ball, glm::vec3(1.0f, 2.0f, 3.0f), 0.5f, false, 0});
    live = flight_after_movement(std::move(live), standing_path(golfer_at(l.feet), launch_ms - 500, 0), false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    live.rolled = {5, 9};

    const std::vector<std::uint8_t> flight_bytes = encode_flight(live.flight);
    const std::optional<ball_flight> flight = decode_flight(flight_bytes);
    REQUIRE(flight);
    CHECK(encode_flight(*flight) == flight_bytes);

    const std::vector<std::uint8_t> bytes = encode_live_flight(live);
    const std::optional<live_flight> back = decode_live_flight(bytes);
    REQUIRE(back);
    CHECK(encode_live_flight(*back) == bytes);
    CHECK(back->pending.size() == 1U);
    CHECK(back->rolled == live.rolled);
    CHECK(back->flags_out == live.flags_out);
    CHECK(back->golfer.levels.at("driving") == 12);
    CHECK(back->golfer.club_speed_scale == 1.05f);
    CHECK(back->record.snapshot.size() == 1U);

    // Resumed from the decoded flight, the contact plays on as from the original.
    const shot_course flown = struck_course(l.record, l.course, tuning);
    const flight_contact contact = live.pending.front();
    const ball_flight from_original = replay_after_contact(live.flight, contact, flown, tuning);
    const ball_flight from_decoded = replay_after_contact(back->flight, contact, flown, tuning);
    CHECK(encode_flight(from_original) == encode_flight(from_decoded));
    CHECK(from_original.contacts.size() == 1U);

    // A cut or padded blob is no flight.
    std::vector<std::uint8_t> cut = bytes;
    cut.pop_back();
    CHECK(!decode_live_flight(cut));
    std::vector<std::uint8_t> padded = bytes;
    padded.push_back(0);
    CHECK(!decode_live_flight(padded));
    CHECK(!decode_flight({}));
}

TEST_CASE("a golfer standing in the line is hit as the snapshot's person was: the same time, place, bounce and outcome") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const flight_env env{l.course, tuning, state.clubs};
    const snapshot_entry person = golfer_at(l.feet);
    shot_record old_record = l.record;
    old_record.snapshot = {person};
    const shot_result reference = simulate_ball(l.ball, struck_course(old_record, l.course, tuning), tuning);
    REQUIRE(!reference.person_hits.empty());

    live_flight live = flight_after_movement(lane_flight(l, tuning), standing_path(person, launch_ms - 1000, 0), false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    CHECK(live.pending.front().time == reference.person_hits.front().time);
    CHECK(live.pending.front().speed_mps == reference.person_hits.front().speed_mps);
    CHECK(live.pending.front().height_m == reference.person_hits.front().contact_height_m);

    // Not applied before the movers have stepped past it, applied once settled.
    const std::int64_t at = step_clock_us(launch_ms, live.pending.front().step + 1) / 1000;
    CHECK(settle_flight(live, at - 1, {}, env).applied.empty());
    const settlement settled = settle_flight(live, at, {known_mover{person, launch_ms - 1000}}, env);
    REQUIRE(settled.applied.size() == 1U);
    CHECK(same_result(settled.live.flight.result, reference));
    const person_hit& hit = reference.person_hits.front();
    CHECK(settled.applied.front().outcome ==
          hit_outcome(hit.speed_mps, hit.contact_height_m, tuning.rangefinder.person_height * tuning.scale.meters_per_world_unit,
                      false, tuning.ball_hits, old_record.seed, 0));
    CHECK(settled.applied.front().outcome == hit_outcome_kind::dazed);
    CHECK(settled.live.rolled == std::vector<std::uint64_t>{7});
}

TEST_CASE("a golfer whose steps take them out of the line is not hit and the ball rests as the static flight does") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const flight_env env{l.course, tuning, state.clubs};
    const snapshot_entry person = golfer_at(l.feet);
    live_flight live = flight_after_movement(lane_flight(l, tuning), standing_path(person, launch_ms - 1000, 0), false, {}, env);
    REQUIRE(live.pending.size() == 1U);

    // Off the line by the time the ball is there (0.24 s).
    const mover_path leaving = moving_path(person, l.feet, glm::vec3(0.0f, 0.0f, 4.0f), launch_ms, 60, tuning);
    live = flight_after_movement(live, leaving, false, {}, env);
    CHECK(live.pending.empty());
    const settlement settled = settle_flight(live, forever, {}, env);
    CHECK(settled.applied.empty());
    CHECK(same_result(settled.live.flight.result, lane_flight(l, tuning).flight.result));
    CHECK(settled.live.flight.contacts.empty());

    // Getting into a cart: no longer on foot, so no longer there.
    live = flight_after_movement(lane_flight(l, tuning), standing_path(person, launch_ms - 1000, 0), false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    mover_path boarded;
    boarded.mover = person;
    boarded.samples = {mover_sample{launch_ms - 500, l.feet, 0.0f}};
    CHECK(flight_after_movement(live, boarded, true, {}, env).pending.empty());
    CHECK(flight_after_movement(live, boarded, false, {}, env).pending.size() == 1U);
}

TEST_CASE("a golfer walking into the line is hit where the step has them, and a cart driven across it deflects the ball") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const flight_env env{l.course, tuning, state.clubs};
    const float unit = tuning.scale.meters_per_world_unit;
    const glm::vec3 beside = l.feet + glm::vec3(0.0f, 0.0f, -1.2f / unit);
    const snapshot_entry person = golfer_at(beside);
    live_flight live = flight_after_movement(lane_flight(l, tuning), standing_path(person, launch_ms - 1000, 0), false, {}, env);
    CHECK(live.pending.empty());

    const mover_path entering = moving_path(person, beside, glm::vec3(0.0f, 0.0f, 4.0f), launch_ms, 60, tuning);
    live = flight_after_movement(live, entering, false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    const flight_contact met = live.pending.front();
    CHECK(met.mover.position.z > beside.z + 0.5f / unit);
    const settlement settled = settle_flight(live, forever, {}, env);
    REQUIRE(settled.applied.size() == 1U);
    shot_record stood_record = l.record;
    stood_record.snapshot = {met.mover};
    CHECK(same_result(settled.live.flight.result, simulate_ball(l.ball, struck_course(stood_record, l.course, tuning), tuning)));
    CHECK(!same_result(settled.live.flight.result, lane_flight(l, tuning).flight.result));

    // A cart, three metres off the line, driven on at 8 m/s.
    const glm::vec3 start = l.feet + glm::vec3(0.0f, 0.0f, -3.0f / unit);
    const snapshot_entry cart{snapshot_kind::cart, start, 0.0f, false, 9};
    live = flight_after_movement(lane_flight(l, tuning), standing_path(cart, launch_ms - 1000, 0), false, {}, env);
    CHECK(live.pending.empty());
    live = flight_after_movement(live, moving_path(cart, start, glm::vec3(0.0f, 0.0f, 8.0f), launch_ms, 60, tuning), false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    CHECK(live.pending.front().mover.kind == snapshot_kind::cart);
    const settlement driven = settle_flight(live, forever, {}, env);
    REQUIRE(driven.applied.size() == 1U);
    CHECK(driven.applied.front().outcome == hit_outcome_kind::none);  // a cart is no one's to knock out
    CHECK(driven.live.rolled.empty());
    CHECK(driven.live.flight.result.rest_position.x < l.feet.x);  // thrown back the way it came
}

TEST_CASE("the earliest contact wins whichever mover reports first, and only a mover's first contact is rolled") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const flight_env env{l.course, tuning, state.clubs};
    const float unit = tuning.scale.meters_per_world_unit;
    const snapshot_entry far_one = golfer_at(l.feet, 7);
    const snapshot_entry near_one = golfer_at(l.feet - glm::vec3(4.0f / unit, 0.0f, 0.0f), 8);
    const auto pending_of = [&](const snapshot_entry& first, const snapshot_entry& second) {
        live_flight live = flight_after_movement(lane_flight(l, tuning), standing_path(first, launch_ms - 1000, 0), false, {}, env);
        live = flight_after_movement(live, standing_path(second, launch_ms - 1000, 0), false, {}, env);
        return live.pending;
    };
    for (const std::vector<flight_contact>& pending : {pending_of(far_one, near_one), pending_of(near_one, far_one)}) {
        REQUIRE(pending.size() == 1U);
        CHECK(pending.front().mover.account == 8U);
    }

    // The ball thrown back meets the same golfer again: that one just bounces.
    live_flight live = flight_after_movement(lane_flight(l, tuning), standing_path(far_one, launch_ms - 1000, 0), false, {}, env);
    settlement first = settle_flight(live, forever, {known_mover{far_one, launch_ms - 1000}}, env);
    REQUIRE(first.applied.size() >= 1U);
    CHECK(first.live.rolled == std::vector<std::uint64_t>{7});
    const std::size_t before = first.live.flight.contacts.size();
    const flight_cursor there = cursor_at(first.live.flight, first.live.flight.contacts.back().step + 15,
                                          struck_course(l.record, l.course, tuning), tuning);
    const glm::vec3 heading = safe_normalize(horizontal(there.ball.velocity), glm::vec3(1.0f, 0.0f, 0.0f));
    const snapshot_entry again =
        golfer_at(there.ball.position + heading * (tuning.player.body_radius + 0.03f) -
                      glm::vec3(0.0f, tuning.scale.ball_physics_radius_meters, 0.0f), 7);
    first.live = flight_after_movement(first.live, standing_path(again, launch_ms - 1000, 0), false, {}, env);
    REQUIRE(first.live.pending.size() == 1U);
    const settlement second = settle_flight(first.live, forever, {}, env);
    REQUIRE(!second.applied.empty());
    CHECK(second.live.flight.contacts.size() > before);
    CHECK(second.applied.front().outcome == hit_outcome_kind::none);
    CHECK(second.live.rolled == std::vector<std::uint64_t>{7});
}

TEST_CASE("a ball that would have holed is not holed by a golfer in its way, and its landing is scheduled from where it rests") {
    game_state state = started_hole();
    still_air(state);
    const game_tuning& tuning = state.tuning;
    const shot_course course = current_shot_course(state);
    const glm::vec3 cup = state.area.cups[course.hole.index].centre;
    const club_definition& putter = *std::find_if(state.clubs.begin(), state.clubs.end(),
                                                  [](const club_definition& club) { return club.club_class == "putter"; });
    const glm::vec3 from = resting_on_terrain(state, cup + glm::vec3(-2.0f, 0.0f, 0.0f));
    golfer_profile golfer;
    golfer.spread_scale = 0.0f;
    std::optional<live_flight> holing;
    const flight_env env{course, tuning, state.clubs};
    for (int i = 5; i <= 100 && !holing; ++i) {
        shot_setup setup = stock_swing(putter, tuning, static_cast<float>(i) * 0.01f, lie_at(state.area, from), yaw_towards(from, cup));
        setup.ball_start = from;
        setup.shot_at = launch_ms;
        live_flight live = begin_flight(shot_record{setup, 1, {}}, golfer, 0, 1, {}, {}, env);
        if (live.flight.result.holed) {
            holing = live;
        }
    }
    REQUIRE(holing);
    const glm::vec3 midway = from + (cup - from) * 0.5f;
    const snapshot_entry golfer_in_line = golfer_at(midway - glm::vec3(0.0f, tuning.scale.ball_physics_radius_meters, 0.0f));
    live_flight live = flight_after_movement(*holing, standing_path(golfer_in_line, launch_ms - 500, 0), false, {}, env);
    REQUIRE(live.pending.size() == 1U);
    const settlement settled = settle_flight(live, forever, {known_mover{golfer_in_line, launch_ms - 500}}, env);
    REQUIRE(!settled.applied.empty());
    CHECK(!settled.live.flight.result.holed);
    CHECK(landing_due_ms(settled.live, tuning.server) == flight_end_ms(settled.live.flight) + 250);
    CHECK(landing_due_ms(*holing, tuning.server) == flight_end_ms(holing->flight) + 250);
    // A pending contact is looked at when it settles, else at the landing.
    CHECK(next_due_ms(live, tuning.server) == settle_due_ms(live.flight, live.pending.front(), tuning.server));
    CHECK(next_due_ms(settled.live, tuning.server) == landing_due_ms(settled.live, tuning.server));
}

TEST_CASE("a mover's flights are ruled out by plain boxes, and those near it by their steps' times") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const live_flight live = lane_flight(l, tuning);
    const std::vector<float> boxes = flight_box_values(live.flight);
    REQUIRE(boxes.size() == live.flight.boxes.size() * 6);
    const snapshot_entry on_line = golfer_at(l.feet);
    CHECK(path_near_flight(boxes, launch_ms, standing_path(on_line, launch_ms - 500, launch_ms + 60'000), tuning));
    CHECK(!path_near_flight(boxes, launch_ms, standing_path(golfer_at(l.feet + glm::vec3(0.0f, 0.0f, 50.0f)), launch_ms - 500,
                                                            launch_ms + 60'000), tuning));
    CHECK(!path_near_flight(boxes, launch_ms, standing_path(on_line, launch_ms + 600'000, launch_ms + 700'000), tuning));

    const std::vector<known_mover> movers{known_mover{on_line, 100}, known_mover{golfer_at(l.feet + glm::vec3(0.0f, 0.0f, 90.0f)), 200},
                                          known_mover{golfer_at(l.feet, 8), 0}};
    CHECK(stepped_times_near(boxes, movers, 5.0f) == std::vector<std::int64_t>{100});
}

namespace {
move_world server_world(const game_state& state) {
    move_world world{state.area, state.world.cart_roads, state.tuning};
    world.walk_speed = state.tuning.player.walk_speed;
    world.spec = my_cart_spec(state);
    world.wetness = state.weather.wetness;
    world.limits = movement_limits_for(state.tuning, world.spec, {}, 0);
    return world;
}

move_request walking_request(const game_state& state, const std::uint64_t first_step, const std::int64_t first_time_ms,
                             const std::uint16_t steps) {
    move_input step;
    step.forward = 1;
    step.yaw = quantise_yaw(state.player.yaw);
    move_request request;
    request.motion.position = state.player.position;
    request.motion.mode = motion_mode::walk;
    request.motion.client_time = static_cast<double>(first_step);
    request.first_step = first_step;
    request.first_time_ms = first_time_ms;
    request.start = state.player.position;
    request.runs = {move_run{step, steps}};
    return request;
}
}

TEST_CASE("movement batches carry their time: future and overlapping ones are refused, late ones play as standing") {
    game_state state = started_game(fixture_hub_course());
    const std::int64_t now = 1'000'000;
    move_world world = server_world(state);
    world.now_ms = now;
    mover_state mover = placed_mover(mover_state{}, state.player.position);
    mover.credit_seconds = state.tuning.server.max_motion_gap_seconds;

    // A batch of 30 steps ending a quarter second in the server's future.
    CHECK(play_move(mover, std::nullopt, 1, walking_request(state, 1, now + 100, 30), 0.0f, world).error == error_invalid_motion);

    // On time: played, each step's place noted from its time, anchored on the last.
    const move_outcome first = play_move(mover, std::nullopt, 1, walking_request(state, 1, now - 200, 30), 0.0f, world);
    REQUIRE(first.error.empty());
    CHECK(first.mover.step_time_ms == step_time_ms(now - 200, 29));
    REQUIRE(first.feet.size() == 30U);
    CHECK(first.feet.front().time_ms == now - 200);
    CHECK(first.feet.back().time_ms == step_time_ms(now - 200, 29));
    CHECK(near(first.feet.back().position, first.mover.position, 0.0001f));

    // Over steps already played: refused; the next steps straight after: placed.
    world.now_ms = now + 100;
    CHECK(play_move(first.mover, std::nullopt, 1, walking_request(state, 31, now - 150, 30), 0.1f, world).error == error_invalid_motion);
    const move_outcome next = play_move(first.mover, std::nullopt, 1, walking_request(state, 31, now - 200 + 250, 30), 0.1f, world);
    REQUIRE(next.error.empty());
    REQUIRE(!next.feet.empty());
    CHECK(next.feet.front().time_ms == first.mover.step_time_ms);  // where they stood until then
    CHECK(near(next.feet.front().position, first.mover.position, 0.0001f));

    // Older than the rewind window: played, but one place held.
    world.now_ms = now + 10'000;
    const move_outcome late = play_move(next.mover, std::nullopt, 1, walking_request(state, 61, now + 500, 30), 10.0f, world);
    REQUIRE(late.error.empty());
    CHECK(late.mover.step > next.mover.step);
    CHECK(late.feet.size() == 2U);
    CHECK(late.feet.front().time_ms == next.mover.step_time_ms);
    CHECK(near(late.feet.back().position, late.mover.position, 0.0001f));

    // No time claimed: played, standing.
    const move_outcome untimed = play_move(late.mover, std::nullopt, 1, walking_request(state, 91, 0, 30), 1.0f, world);
    REQUIRE(untimed.error.empty());
    CHECK(untimed.mover.step_time_ms == late.mover.step_time_ms);
    CHECK(untimed.feet.size() <= 2U);
}
