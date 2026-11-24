#include "doctest.h"

#include "game/ball_contact.h"
#include "game/contact_settle.h"
#include "game/shot_simulation.h"
#include "game/step_timeline.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cstring>
#include <optional>
#include <vector>

namespace {
constexpr std::int64_t launch_ms = 5'000'000;

bool same_bits(const float a, const float b) {
    return std::memcmp(&a, &b, sizeof a) == 0;
}

bool same_bits(const glm::vec3& a, const glm::vec3& b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z);
}

bool same_ball(const ball_state& a, const ball_state& b) {
    return same_bits(a.position, b.position) && same_bits(a.velocity, b.velocity) && same_bits(a.spin, b.spin) &&
        same_bits(a.radius, b.radius);
}

// Every part of the two shots equal to the bit, but who a person hit was.
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
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (!same_bits(a.events[i].time, b.events[i].time) || a.events[i].kind != b.events[i].kind ||
            a.events[i].material != b.events[i].material) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.person_hits.size(); ++i) {
        if (!same_bits(a.person_hits[i].time, b.person_hits[i].time) || !same_bits(a.person_hits[i].speed_mps, b.person_hits[i].speed_mps) ||
            !same_bits(a.person_hits[i].contact_height_m, b.person_hits[i].contact_height_m)) {
            return false;
        }
    }
    return same_bits(a.rest_position, b.rest_position) && same_bits(a.duration, b.duration) && a.holed == b.holed &&
        a.lost == b.lost && a.penalty_strokes == b.penalty_strokes;
}

bool same_flight(const ball_flight& a, const ball_flight& b) {
    if (a.steps != b.steps || a.open_from_step != b.open_from_step || a.checkpoints.size() != b.checkpoints.size() ||
        a.boxes.size() != b.boxes.size() || a.contacts.size() != b.contacts.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.checkpoints.size(); ++i) {
        const flight_checkpoint& x = a.checkpoints[i];
        const flight_checkpoint& y = b.checkpoints[i];
        if (!same_ball(x.cursor.ball, y.cursor.ball) || x.cursor.steps != y.cursor.steps ||
            x.event_count != y.event_count || x.hit_count != y.hit_count || x.cursor.in_contact != y.cursor.in_contact) {
            return false;
        }
    }
    for (std::size_t i = 0; i < a.boxes.size(); ++i) {
        if (!same_bits(a.boxes[i].low, b.boxes[i].low) || !same_bits(a.boxes[i].high, b.boxes[i].high)) {
            return false;
        }
    }
    return same_result(a.result, b.result);
}

// A ball flying straight along +X in still air, `height` over the ground,
// and where a golfer standing in its way, 10 m on, has their feet.
struct lane {
    shot_course course;  // nothing on it that moves
    ball_state ball;
    glm::vec3 feet{0.0f};
};

lane flat_lane(game_state& state, const float speed_mps, const float height) {
    still_air(state);
    lane made{current_shot_course(state), still_ball(state), glm::vec3(0.0f)};
    made.feet = resting_on_terrain(state, state.ball->position + glm::vec3(10.0f, 0.0f, 0.0f)) -
        glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f);
    made.ball.position = made.feet + glm::vec3(-10.0f, height, 0.0f);
    made.ball.velocity = glm::vec3(speed_mps / state.tuning.scale.meters_per_world_unit, 0.0f, 0.0f);
    return made;
}

snapshot_entry golfer_at(const glm::vec3& feet, const bool shooter = false) {
    return snapshot_entry{snapshot_kind::person, feet, 0.0f, shooter, 7};
}

snapshot_entry cart_at(const glm::vec3& position, const float yaw) {
    return snapshot_entry{snapshot_kind::cart, position, yaw, false, 9};
}

// `who` moving from `from` at `velocity_mps` (a step every move_step_seconds, from `from_ms`), `steps` steps.
mover_path moving_path(const snapshot_entry& who, const glm::vec3& from, const glm::vec3& velocity_mps, const std::int64_t from_ms,
                       const int steps, const game_tuning& tuning) {
    mover_path path;
    path.mover = who;
    for (int i = 0; i <= steps; ++i) {
        const float seconds = static_cast<float>(i) * move_step_seconds;
        path.samples.push_back(mover_sample{step_time_ms(from_ms, static_cast<std::uint64_t>(i)),
                                            from + velocity_mps / tuning.scale.meters_per_world_unit * seconds, who.yaw});
    }
    path.until_ms = path.samples.back().time_ms + 9;
    return path;
}

shot_course with_solid(const shot_course& course, const solid& thing) {
    shot_course met = course;
    met.dynamic.push_back(thing);
    return met;
}

// Applies every contact `path` makes with `flight`, one after another.
ball_flight play_path(ball_flight flight, const mover_path& path, const shot_course& course, const game_tuning& tuning,
                      std::vector<flight_contact>* found = nullptr) {
    for (int i = 0; i < 20; ++i) {
        const std::optional<flight_contact> contact = first_contact(flight, path, course, tuning);
        if (!contact) {
            break;
        }
        if (found != nullptr) {
            found->push_back(*contact);
        }
        flight = replay_after_contact(flight, *contact, course, tuning);
    }
    return flight;
}

const club_definition& club_labelled(const game_state& state, const std::string& label) {
    return *std::find_if(state.clubs.begin(), state.clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

shot_setup aimed(const game_state& state, const std::string& label, const glm::vec3& ball, const glm::vec3& target,
                 const float backswing) {
    shot_setup setup = stock_swing(club_labelled(state, label), state.tuning, backswing, lie_at(state.area, ball),
                                   yaw_towards(ball, target));
    setup.ball_start = ball;
    setup.shot_at = launch_ms;
    return setup;
}

golfer_profile steady() {
    golfer_profile golfer;
    golfer.spread_scale = 0.0f;
    return golfer;
}
}

TEST_CASE("step times are the first time plus whole steps to the nearest millisecond, never drifting") {
    CHECK(step_time_ms(1000, 0) == 1000);
    CHECK(step_time_ms(1000, 1) == 1008);
    CHECK(step_time_ms(1000, 2) == 1017);
    CHECK(step_time_ms(1000, 3) == 1025);
    CHECK(step_time_ms(1000, 120) == 2000);
    CHECK(step_time_ms(1000, 120 * 3600) == 1000 + 3'600'000);
    for (std::uint64_t i = 0; i < 5000; ++i) {
        const std::int64_t gap = step_time_ms(0, i + 1) - step_time_ms(0, i);
        CHECK((gap == 8 || gap == 9));
    }
}

TEST_CASE("batches placed one after another are never refused for rounding, whatever their size") {
    const server_tuning& server = shipped_content().tuning.server;
    std::optional<timeline_anchor> anchor;
    std::uint64_t step = 0;
    const std::int64_t first = 10'000'000;
    for (const std::uint32_t size : {7u, 1u, 13u, 2u, 40u, 5u, 9u, 120u, 3u}) {
        const timed_batch batch{step, size, step_time_ms(first, step)};
        const timeline_result placed = place_batch(anchor, batch, first + 100'000, server);
        REQUIRE(placed.verdict == timeline_verdict::accepted);
        CHECK(!placed.repeated);
        CHECK(placed.anchor.step == step + size - 1);
        CHECK(placed.anchor.time_ms == step_time_ms(batch.first_time_ms, size - 1));
        anchor = placed.anchor;
        step += size;
    }
}

TEST_CASE("a batch in the server's future, overlapping played steps or long past is refused, placed or counted late") {
    const server_tuning& server = shipped_content().tuning.server;
    const std::int64_t slack = 250;
    const std::int64_t now = 100'000;
    REQUIRE(static_cast<std::int64_t>(server.timing_slack_seconds * 1000.0f) == slack);
    REQUIRE(static_cast<std::int64_t>(server.contact_rewind_seconds * 1000.0f) == 250);

    // The future is the last step's time past now and the slack.
    const std::int64_t last_offset = step_time_ms(0, 11);
    CHECK(place_batch(std::nullopt, timed_batch{0, 12, now + slack - last_offset}, now, server).verdict == timeline_verdict::accepted);
    CHECK(place_batch(std::nullopt, timed_batch{0, 12, now + slack - last_offset + 1}, now, server).verdict == timeline_verdict::future);

    // Late: every step ended before now minus the rewind window.
    CHECK(!place_batch(std::nullopt, timed_batch{0, 12, now - 250 - last_offset}, now, server).late);
    CHECK(place_batch(std::nullopt, timed_batch{0, 12, now - 251 - last_offset}, now, server).late);
    const timeline_result late = place_batch(std::nullopt, timed_batch{0, 12, now - 2000}, now, server);
    CHECK(late.verdict == timeline_verdict::accepted);
    CHECK(late.late);
    CHECK(late.anchor.step == 11);
    CHECK(late.anchor.time_ms == now - 2000 + last_offset);

    // Overlap: the next step at least a step's length (rounded down, less a millisecond for
    // the client's rounding) on.
    const timeline_anchor played{11, 99'992};
    CHECK(place_batch(played, timed_batch{12, 6, 99'998}, now, server).verdict == timeline_verdict::overlap);
    CHECK(place_batch(played, timed_batch{12, 6, 99'999}, now, server).verdict == timeline_verdict::accepted);
    CHECK(place_batch(played, timed_batch{14, 6, 100'015}, now, server).verdict == timeline_verdict::overlap);
    CHECK(place_batch(played, timed_batch{14, 6, 100'016}, now, server).verdict == timeline_verdict::accepted);
    CHECK(place_batch(played, timed_batch{3, 6, 99'900}, now, server).verdict == timeline_verdict::accepted);

    // Sent again: nothing new, the anchor stays. Partly new: the first new step is checked.
    const timeline_result repeated = place_batch(played, timed_batch{6, 6, 99'950}, now, server);
    CHECK(repeated.verdict == timeline_verdict::accepted);
    CHECK(repeated.repeated);
    CHECK(repeated.anchor.step == 11);
    CHECK(place_batch(played, timed_batch{9, 6, 99'973}, now, server).verdict == timeline_verdict::overlap);
    CHECK(place_batch(played, timed_batch{9, 6, 99'974}, now, server).verdict == timeline_verdict::accepted);
}

TEST_CASE("a flight resumed from any checkpoint flies the same shot to the bit") {
    game_state state = started_hole();
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 target = ball + yaw_direction(state.player.yaw) * 150.0f;
    const shot_record record{aimed(state, "DR", ball, target, 1.0f), 7, {}};
    const shot_course static_course = struck_course(record, current_shot_course(state), state.tuning);
    const ball_flight flight = fly_struck_shot(record, steady(), current_shot_course(state), state.tuning, state.clubs);
    REQUIRE(flight.steps > 6 * flight_chunk_steps);
    CHECK(flight.checkpoints.size() == static_cast<std::size_t>((flight.steps - 1) / flight_checkpoint_steps + 1));
    CHECK(flight.boxes.size() == static_cast<std::size_t>((flight.steps - 1) / flight_chunk_steps + 1));

    // It is the one simulate_ball flies.
    const shot_result plain = simulate_struck_shot(record, steady(), current_shot_course(state), state.tuning, state.clubs);
    CHECK(same_result(flight.result, plain));
    CHECK(same_bits(flight.result.strike_centredness, plain.strike_centredness));

    // Every step's ball from the nearest checkpoint is the trajectory's.
    for (int step = 0; step < flight.steps; ++step) {
        const flight_cursor cursor = cursor_at(flight, step, static_course, state.tuning);
        REQUIRE(cursor.steps == step);
        if (step % shot_steps_per_trajectory_point == 0) {
            REQUIRE(same_bits(cursor.ball.position, flight.result.trajectory[static_cast<std::size_t>(step / shot_steps_per_trajectory_point)]));
        }
    }
    // Resumed from every chunk, the whole flight is the original.
    for (int step = 0; step < flight.steps; step += flight_chunk_steps) {
        const ball_flight resumed = resume_flight(flight, step, flight.contacts, static_course, state.tuning);
        REQUIRE(same_flight(resumed, flight));
    }
    // The boxes hold the path.
    for (int step = 0; step < flight.steps; ++step) {
        const glm::vec3 at = cursor_at(flight, step, static_course, state.tuning).ball.position;
        const flight_box& box = flight.boxes[static_cast<std::size_t>(step / flight_chunk_steps)];
        REQUIRE(at.x >= box.low.x);
        REQUIRE(at.y >= box.low.y);
        REQUIRE(at.z >= box.low.z);
        REQUIRE(at.x <= box.high.x);
        REQUIRE(at.y <= box.high.y);
        REQUIRE(at.z <= box.high.z);
    }
}

TEST_CASE("a golfer standing in the line is hit as the snapshot's person is, at the same step, time and place") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const snapshot_entry person = golfer_at(l.feet);
    const shot_result reference = simulate_ball(l.ball, with_solid(l.course, snapshot_solid(person, state.tuning)), state.tuning);
    REQUIRE(!reference.person_hits.empty());

    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, state.tuning);
    CHECK(flight.result.person_hits.empty());
    const mover_path path = standing_path(person, launch_ms - 1000, launch_ms + 60'000);
    std::vector<flight_contact> contacts;
    const ball_flight played = play_path(flight, path, l.course, state.tuning, &contacts);
    REQUIRE(!contacts.empty());
    CHECK(contacts.front().time == reference.person_hits.front().time);
    CHECK(contacts.front().speed_mps == reference.person_hits.front().speed_mps);
    CHECK(contacts.front().height_m == reference.person_hits.front().contact_height_m);
    CHECK(contacts.front().mover.kind == snapshot_kind::person);
    CHECK(contacts.front().mover.account == 7U);
    CHECK(same_bits(contacts.front().mover.position, l.feet));
    CHECK(same_result(played.result, reference));
    CHECK(played.result.person_hits.front().entry == 0U);
    CHECK(played.contacts.size() == contacts.size());
}

TEST_CASE("a golfer who steps out of the line before the ball arrives is not hit, one who walks in is, where the walk is then") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, tuning);
    const float unit = tuning.scale.meters_per_world_unit;

    // In the line at the launch, off it by the time the ball is there (0.24 s).
    const snapshot_entry out = golfer_at(l.feet);
    CHECK(first_contact(flight, standing_path(out, launch_ms, launch_ms + 5000), l.course, tuning));
    contact_search_stats stats;
    const mover_path leaving = moving_path(out, l.feet, glm::vec3(0.0f, 0.0f, 4.0f), launch_ms, 60, tuning);
    CHECK(!first_contact(flight, leaving, l.course, tuning, &stats));
    CHECK(stats.steps_confirmed == 0);

    // Beside the line, then walking into it.
    const glm::vec3 beside = l.feet + glm::vec3(0.0f, 0.0f, -1.2f / unit);
    const snapshot_entry in = golfer_at(beside);
    CHECK(!first_contact(flight, standing_path(in, launch_ms, launch_ms + 5000), l.course, tuning));
    const mover_path entering = moving_path(in, beside, glm::vec3(0.0f, 0.0f, 4.0f), launch_ms, 60, tuning);
    const std::optional<flight_contact> contact = first_contact(flight, entering, l.course, tuning);
    REQUIRE(contact);
    CHECK(contact->time > 0.2f);
    CHECK(contact->time < 0.26f);
    const std::optional<snapshot_entry> there = mover_at(entering, step_clock_us(launch_ms, contact->step + 1));
    REQUIRE(there);
    CHECK(same_bits(contact->mover.position, there->position));
    CHECK(contact->mover.position.z > beside.z + 0.5f / unit);

    // The ball bounces off the golfer as they stood there then.
    snapshot_entry stood = *there;
    const shot_result reference = simulate_ball(l.ball, with_solid(l.course, snapshot_solid(stood, tuning)), tuning);
    REQUIRE(!reference.person_hits.empty());
    CHECK(reference.person_hits.front().time == contact->time);
    const ball_flight played = replay_after_contact(flight, *contact, l.course, tuning);
    CHECK(same_result(played.result, reference));
}

TEST_CASE("a ball too fast to land inside a thin golfer between steps still meets them: the step's path is swept") {
    game_state state = started_hole();
    lane l = flat_lane(state, 200.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const float unit = tuning.scale.meters_per_world_unit;
    const float per_step = 200.0f * shot_step_seconds;
    // Step 6 ends at -0.9 m and step 7 at 0.77 m: either side of a 0.6 m wide golfer at 0.
    l.ball.position = l.feet + glm::vec3((-0.9f - 6.0f * per_step) / unit, 1.0f, 0.0f);
    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, tuning);
    const float reach = (tuning.player.body_radius + l.ball.radius);
    for (int step = 0; step < 14; ++step) {
        const glm::vec3 at = cursor_at(flight, step, l.course, tuning).ball.position;
        REQUIRE(horizontal_distance(at, l.feet) > reach);
    }
    const snapshot_entry person = golfer_at(l.feet);
    const std::optional<flight_contact> contact = first_contact(flight, standing_path(person, launch_ms, launch_ms + 5000), l.course, tuning);
    REQUIRE(contact);
    CHECK(contact->step == 6);
    CHECK(contact->speed_mps > 190.0f);
    const shot_result reference = simulate_ball(l.ball, with_solid(l.course, snapshot_solid(person, tuning)), tuning);
    CHECK(same_result(replay_after_contact(flight, *contact, l.course, tuning).result, reference));
}

TEST_CASE("a moving cart is hit and the ball bounces as from the cart's solid where it was") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const float unit = tuning.scale.meters_per_world_unit;
    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, tuning);

    // Facing +Z, a cart's box reaches 1.38 m ahead of the point it is placed by and
    // 1.02 behind: at 3 m back it stands clear of the ball's line, driving on at 8 m/s it is across it.
    const glm::vec3 start = l.feet + glm::vec3(0.0f, 0.0f, -3.0f / unit);
    const snapshot_entry cart = cart_at(start, 0.0f);
    CHECK(!first_contact(flight, standing_path(cart, launch_ms, launch_ms + 5000), l.course, tuning));
    const mover_path driving = moving_path(cart, start, glm::vec3(0.0f, 0.0f, 8.0f), launch_ms, 60, tuning);
    const std::optional<flight_contact> contact = first_contact(flight, driving, l.course, tuning);
    REQUIRE(contact);
    CHECK(contact->mover.kind == snapshot_kind::cart);
    CHECK(contact->mover.account == 9U);
    CHECK(contact->mover.position.z > start.z + 1.0f / unit);

    const ball_flight played = replay_after_contact(flight, *contact, l.course, tuning);
    const solid cart_where_it_was = snapshot_solid(contact->mover, tuning);
    CHECK(cart_where_it_was.kind == solid_kind::cart);
    const shot_result reference = simulate_ball(l.ball, with_solid(l.course, cart_where_it_was), tuning);
    CHECK(same_result(played.result, reference));
    const auto board = std::find_if(played.result.events.begin(), played.result.events.end(),
                                    [](const shot_event& event) { return event.kind == shot_event_kind::board_hit; });
    REQUIRE(board != played.result.events.end());
    CHECK(board->time == contact->time);
    CHECK(played.result.person_hits.empty());
    // It is thrown back the way it came.
    CHECK(cursor_at(played, contact->step + 3, with_solid(l.course, cart_where_it_was), tuning).ball.velocity.x < 0.0f);
}

TEST_CASE("the shooter standing over the ball is not met within the grace, anyone else there is") {
    game_state state = started_hole();
    lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    l.ball.position = l.feet + glm::vec3(-0.5f, 1.0f, 0.0f);
    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, tuning);
    CHECK(!first_contact(flight, standing_path(golfer_at(l.feet, true), launch_ms, launch_ms + 5000), l.course, tuning));
    const std::optional<flight_contact> other = first_contact(flight, standing_path(golfer_at(l.feet, false), launch_ms, launch_ms + 5000), l.course, tuning);
    REQUIRE(other);
    CHECK(other->time < 0.05f);
}

TEST_CASE("a mover far off or out of time is ruled out by the boxes alone, without stepping the ball") {
    game_state state = started_hole();
    const lane l = flat_lane(state, 40.0f, 1.0f);
    const game_tuning& tuning = state.tuning;
    const float unit = tuning.scale.meters_per_world_unit;
    const ball_flight flight = fly_ball(l.ball, launch_ms, l.course, tuning);
    REQUIRE(!flight.boxes.empty());

    const std::vector<mover_path> clear = {
        standing_path(golfer_at(l.feet + glm::vec3(0.0f, 0.0f, 200.0f / unit)), launch_ms - 1000, launch_ms + 60'000),
        standing_path(golfer_at(l.feet + glm::vec3(0.0f, 0.0f, 5.0f / unit)), launch_ms - 1000, launch_ms + 60'000),
        standing_path(golfer_at(l.feet), launch_ms + 100'000, launch_ms + 160'000),
        standing_path(golfer_at(l.feet), launch_ms - 100'000, launch_ms - 50'000),
        moving_path(golfer_at(l.feet), l.feet + glm::vec3(0.0f, 0.0f, 20.0f / unit), glm::vec3(0.0f, 0.0f, 4.0f), launch_ms, 60, tuning)};
    for (const mover_path& path : clear) {
        contact_search_stats stats;
        CHECK(!first_contact(flight, path, l.course, tuning, &stats));
        CHECK(stats.chunks_tested >= 1);
        CHECK(stats.chunks_overlapping == 0);
        CHECK(stats.steps_tested == 0);
        CHECK(stats.steps_confirmed == 0);
    }

    // Near enough for the boxes, the exact test is only asked about steps near the golfer.
    contact_search_stats near_miss;
    CHECK(!first_contact(flight, standing_path(golfer_at(l.feet + glm::vec3(0.0f, 0.0f, 0.33f / unit)), launch_ms - 1000, launch_ms + 60'000),
                         l.course, tuning, &near_miss));
    CHECK(near_miss.chunks_overlapping >= 1);
    CHECK(near_miss.steps_tested <= 2 * flight_chunk_steps);
    CHECK(near_miss.steps_confirmed <= 3);
}

TEST_CASE("a mover's path: the place held from each step, its span and the box it sweeps") {
    game_state state = started_hole();
    const game_tuning& tuning = state.tuning;
    const glm::vec3 from(0.0f, 0.0f, 0.0f);
    const mover_path path = moving_path(golfer_at(from), from, glm::vec3(3.0f, 0.0f, 0.0f), 1000, 12, tuning);
    CHECK(!mover_at(path, 999'999));
    CHECK(mover_at(path, 1'000'000)->position.x == 0.0f);
    CHECK(mover_at(path, 1'007'999)->position.x == 0.0f);
    CHECK(mover_at(path, 1'008'000)->position.x > 0.0f);
    CHECK(mover_at(path, 1'008'000)->account == 7U);
    CHECK(!mover_at(path, path.until_ms * 1000));
    const std::optional<world_box> whole = swept_box(path, 1'000'000, path.until_ms * 1000, tuning);
    REQUIRE(whole);
    const float last = path.samples.back().position.x;
    CHECK(near(whole->low.x, -tuning.player.body_radius));
    CHECK(near(whole->high.x, last + tuning.player.body_radius));
    CHECK(near(whole->high.y, tuning.rangefinder.person_height));
    // Just the first step: the golfer where they began.
    const std::optional<world_box> first = swept_box(path, 1'000'000, 1'000'500, tuning);
    REQUIRE(first);
    CHECK(near(first->high.x, tuning.player.body_radius));
    CHECK(!swept_box(path, 0, 999'000, tuning));
    CHECK(!swept_box(path, path.until_ms * 1000, path.until_ms * 1000 + 1000, tuning));
    const world_box cart = *swept_box(standing_path(cart_at(from, 0.0f), 0, 1000), 0, 500'000, tuning);
    CHECK(near(cart.high.z, tuning.cart.body_front));
    CHECK(near(cart.low.z, -tuning.cart.body_back));
    CHECK(near(cart.high.x, tuning.cart.body_half_width));
}

TEST_CASE("a snapshot splits into what is still and what moves") {
    const std::vector<snapshot_entry> entries{
        snapshot_entry{snapshot_kind::ball, glm::vec3(1.0f), 0.0f, false, 0},
        snapshot_entry{snapshot_kind::person, glm::vec3(2.0f), 0.0f, true, 5},
        snapshot_entry{snapshot_kind::body, glm::vec3(3.0f), 1.0f, false, 6},
        snapshot_entry{snapshot_kind::cart, glm::vec3(4.0f), 2.0f, false, 8}};
    const snapshot_split split = split_snapshot(entries);
    REQUIRE(split.still.size() == 2U);
    CHECK(split.still[0].kind == snapshot_kind::ball);
    CHECK(split.still[1].kind == snapshot_kind::body);
    REQUIRE(split.movers.size() == 2U);
    CHECK(split.movers[0].kind == snapshot_kind::person);
    CHECK(split.movers[0].account == 5U);
    CHECK(split.movers[0].shooter);
    CHECK(split.movers[1].kind == snapshot_kind::cart);
    CHECK(split.movers[1].account == 8U);
}

TEST_CASE("settling: movers' steps and the rewind window, the earliest contact winning, the scheduled check") {
    const server_tuning& server = shipped_content().tuning.server;
    const std::int64_t now = 100'000;
    CHECK(settled_until_ms({99'990, 99'800}, now, server) == 99'800);
    CHECK(settled_until_ms({99'000, 99'900}, now, server) == 99'750);
    CHECK(settled_until_ms({}, now, server) == 99'750);
    CHECK(settled_until_ms({100'500}, now, server) == now);

    ball_flight flight;
    flight.launch_ms = 20'000;
    flight_contact contact;
    contact.step = 29;
    CHECK(contact_settled(flight, contact, 20'250));
    CHECK(!contact_settled(flight, contact, 20'249));
    contact.step = 30;
    CHECK(!contact_settled(flight, contact, 20'250));
    CHECK(settle_due_ms(flight, contact, server) == 20'259 + 250);

    flight_contact early;
    early.step = 40;
    flight_contact late;
    late.step = 90;
    flight_contact same_step;
    same_step.step = 40;
    same_step.mover.account = 3;
    CHECK(pending_with({}, late).size() == 1U);
    const std::vector<flight_contact> pending = pending_with(pending_with({}, late), early);
    REQUIRE(pending.size() == 1U);
    CHECK(pending[0].step == 40);
    const std::vector<flight_contact> kept = pending_with(pending, late);
    REQUIRE(kept.size() == 1U);
    CHECK(kept[0].step == 40);
    const std::vector<flight_contact> tied = pending_with(pending, same_step);
    REQUIRE(tied.size() == 1U);
    CHECK(tied[0].mover.account == 0U);
}

TEST_CASE("a shot rebuilt from its record and its contacts is the flight the server played on, contact by contact") {
    game_state state = started_hole();
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 ahead = ball + yaw_direction(state.player.yaw) * 3.0f;
    const shot_record record{aimed(state, "P", ball, ahead, 0.6f), 7, {}};
    const shot_course course = current_shot_course(state);
    const shot_course flown = struck_course(record, course, state.tuning);
    const game_tuning& tuning = state.tuning;

    const ball_flight swing = fly_struck_shot(record, steady(), course, tuning, state.clubs);
    REQUIRE(swing.steps > 3 * flight_chunk_steps);

    // A golfer standing on the line, then another where the ball is thrown back to.
    const snapshot_entry first = golfer_at(resting_on_terrain(state, ahead) - glm::vec3(0.0f, tuning.scale.ball_physics_radius_meters, 0.0f));
    std::vector<flight_contact> contacts;
    const std::optional<flight_contact> met = first_contact(swing, standing_path(first, launch_ms - 500, launch_ms + 600'000), flown, tuning);
    REQUIRE(met);
    contacts.push_back(*met);
    ball_flight server = replay_after_contact(swing, *met, flown, tuning);
    CHECK(contacts.front().mover.kind == snapshot_kind::person);
    const int first_step = met->step;
    const int later = first_step + 15;
    REQUIRE(later < server.steps);
    const flight_cursor there = cursor_at(server, later, flown, tuning);
    REQUIRE(glm::length(there.ball.velocity) > 0.05f);
    const glm::vec3 heading = safe_normalize(horizontal(there.ball.velocity), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 base = there.ball.position + heading * (tuning.player.body_radius + 0.03f) -
        glm::vec3(0.0f, tuning.scale.ball_physics_radius_meters, 0.0f);
    const snapshot_entry second = snapshot_entry{snapshot_kind::person, base, 0.0f, false, 11};
    const std::size_t before = contacts.size();
    server = play_path(server, standing_path(second, launch_ms - 500, launch_ms + 600'000), flown, tuning, &contacts);
    REQUIRE(contacts.size() > before);
    CHECK(contacts[before].mover.account == 11U);
    CHECK(contacts[before].step > first_step);

    const ball_flight client = replay_contacts(record, steady(), course, tuning, state.clubs, contacts);
    CHECK(same_flight(client, server));
    CHECK(client.contacts.size() == contacts.size());
    // Up to the first contact the flight is the swing's.
    for (int i = 0; i < contacts.front().step / shot_steps_per_trajectory_point; ++i) {
        REQUIRE(same_bits(client.result.trajectory[static_cast<std::size_t>(i)], swing.result.trajectory[static_cast<std::size_t>(i)]));
    }
    // Contacts played in one at a time to the same flight each time.
    ball_flight stepwise = swing;
    for (const flight_contact& contact : contacts) {
        stepwise = replay_after_contact(stepwise, contact, flown, tuning);
    }
    CHECK(same_flight(stepwise, client));
}
