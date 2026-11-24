#include "doctest.h"

#include "game/contact_prediction.h"
#include "game/motion_sync.h"
#include "game/step_timeline.h"
#include "physics/vector_math.h"

#include "test_support.h"

namespace {
constexpr std::int64_t launch_ms = 7'000'000;

// A ball flying straight along +X, 1 m over the ground, in still air; `feet` is where a golfer 10 m on stands.
struct lane {
    shot_course course;
    ball_flight flight;
    glm::vec3 feet{0.0f};
};

lane flat_lane(game_state& state) {
    still_air(state);
    lane made{current_shot_course(state), {}, glm::vec3(0.0f)};
    ball_state ball = still_ball(state);
    made.feet = resting_on_terrain(state, state.ball->position + glm::vec3(10.0f, 0.0f, 0.0f)) -
        glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f);
    ball.position = made.feet + glm::vec3(-10.0f, 1.0f, 0.0f);
    ball.velocity = glm::vec3(40.0f / state.tuning.scale.meters_per_world_unit, 0.0f, 0.0f);
    made.flight = fly_ball(ball, launch_ms, made.course, state.tuning);
    return made;
}

mover_sample at(const std::int64_t time_ms, const glm::vec3& position) {
    return mover_sample{time_ms, position, 0.0f};
}

snapshot_entry me() {
    return snapshot_entry{snapshot_kind::person, glm::vec3(0.0f), 0.0f, false, 1};
}
}

TEST_CASE("my path log keeps what changed, the last sample before the window and restarts when I change kind") {
    my_path_log log;
    note_path_sample(log, snapshot_kind::person, at(1000, glm::vec3(1.0f)), 0);
    note_path_sample(log, snapshot_kind::person, at(1010, glm::vec3(1.0f)), 0);  // standing: one sample
    CHECK(log.samples.size() == 1U);
    note_path_sample(log, snapshot_kind::person, at(1020, glm::vec3(2.0f)), 0);
    note_path_sample(log, snapshot_kind::person, at(1030, glm::vec3(3.0f)), 0);
    note_path_sample(log, snapshot_kind::person, at(1040, glm::vec3(4.0f)), 1025);
    // 1020 holds the place until 1030, so it stays; 1000 and 1010's are before the window.
    REQUIRE(log.samples.size() == 3U);
    CHECK(log.samples.front().time_ms == 1020);
    // A time that goes back is held at the last.
    note_path_sample(log, snapshot_kind::person, at(1000, glm::vec3(5.0f)), 0);
    CHECK(log.samples.back().time_ms == 1040);
    note_path_sample(log, snapshot_kind::cart, at(1050, glm::vec3(9.0f)), 0);
    CHECK(log.kind == snapshot_kind::cart);
    CHECK(log.samples.size() == 1U);
}

TEST_CASE("the logged path is what I have stepped, as the kind I am, held up to now and not beyond") {
    my_path_log log;
    note_path_sample(log, snapshot_kind::cart, at(1000, glm::vec3(1.0f)), 0);
    snapshot_entry who = me();
    who.shooter = true;
    const mover_path path = logged_path(log, who, 1200);
    CHECK(path.mover.kind == snapshot_kind::cart);
    CHECK(path.mover.shooter);
    CHECK(path.mover.account == 1U);
    CHECK(path.until_ms == 1201);
    CHECK(mover_at(path, 1200 * 1000).has_value());
    CHECK(!mover_at(path, 1201 * 1000).has_value());
    CHECK(!mover_at(path, 999 * 1000).has_value());
}

TEST_CASE("a predicted contact is found only where my steps meet the flight, as the server finds it") {
    game_state state = started_hole();
    const lane made = flat_lane(state);
    const std::int64_t reaches_ms = launch_ms + 280;  // the ball is about 10 m on after a quarter second
    const shot_course& flown = made.course;

    my_path_log in_line;
    note_path_sample(in_line, snapshot_kind::person, at(launch_ms - 200, made.feet), 0);
    const std::optional<flight_contact> hit = first_contact(made.flight, logged_path(in_line, me(), reaches_ms + 400), flown, state.tuning);
    REQUIRE(hit);
    CHECK(hit->mover.account == 1U);
    CHECK(hit->height_m > 0.0f);

    // Stepped out of the line before the ball got there: no contact.
    my_path_log stepped_out;
    note_path_sample(stepped_out, snapshot_kind::person, at(launch_ms - 200, made.feet), 0);
    note_path_sample(stepped_out, snapshot_kind::person,
                     at(launch_ms + 100, made.feet + glm::vec3(0.0f, 0.0f, 3.0f / state.tuning.scale.meters_per_world_unit)), 0);
    CHECK(!first_contact(made.flight, logged_path(stepped_out, me(), reaches_ms + 400), flown, state.tuning));

    // Not before it was stepped to: a path logged only up to a moment before the ball gets there has no contact yet.
    CHECK(!first_contact(made.flight, logged_path(in_line, me(), launch_ms + 50), flown, state.tuning));
}

TEST_CASE("a prediction is overdue its row once its time, the rewind window and the row's way here have passed") {
    game_state state = started_hole();
    const lane made = flat_lane(state);
    flight_contact contact;
    contact.step = 33;
    const std::int64_t at_ms = step_clock_us(launch_ms, contact.step + 1) / 1000;
    const net_tuning& net = state.tuning.net;
    const std::int64_t hold = static_cast<std::int64_t>(net.contact_prediction_hold_seconds * 1000.0f);
    REQUIRE(net.contact_prediction_hold_seconds > state.tuning.server.contact_rewind_seconds);
    CHECK(!prediction_overdue(made.flight, contact, at_ms, net));
    CHECK(!prediction_overdue(made.flight, contact, at_ms + hold - 1, net));
    CHECK(prediction_overdue(made.flight, contact, at_ms + hold, net));
}

TEST_CASE("a ball in flight is near me by its trajectory and by its boxes for the time still to come") {
    game_state state = started_hole();
    const lane made = flat_lane(state);
    const float reach = state.tuning.net.contact_send_meters / state.tuning.scale.meters_per_world_unit;
    const glm::vec3 beside = made.feet + glm::vec3(0.0f, 0.0f, 20.0f / state.tuning.scale.meters_per_world_unit);
    const glm::vec3 far = made.feet + glm::vec3(0.0f, 0.0f, 200.0f / state.tuning.scale.meters_per_world_unit);
    CHECK(trajectory_near(made.flight.result, beside, reach));
    CHECK(!trajectory_near(made.flight.result, far, reach));
    CHECK(flight_near(made.flight, beside, reach, launch_ms));
    CHECK(!flight_near(made.flight, far, reach, launch_ms));
    // Once the flight is over, no box is still to come.
    CHECK(!flight_near(made.flight, beside, reach, launch_ms + 60'000));
}

TEST_CASE("near a ball in flight movement goes every contact interval, otherwise every motion interval") {
    const net_tuning net = shipped_content().tuning.net;
    REQUIRE(net.contact_send_interval_seconds < net.motion_min_interval_seconds);
    net_motion walking;
    walking.mode = motion_mode::walk;
    walking.speed = 1.5f;
    walking.position = glm::vec3(5.0f, 0.0f, 0.0f);
    net_motion earlier = walking;
    earlier.position = glm::vec3(0.0f);
    motion_sync_state sync;
    sync.last_sent = earlier;
    sync.unsent.push_back(move_run{});
    sync.since_sent = net.contact_send_interval_seconds + 0.001f;
    CHECK(!should_send_motion(sync, walking, net));
    sync.near_flight = true;
    CHECK(should_send_motion(sync, walking, net));
    sync.since_sent = net.contact_send_interval_seconds - 0.001f;
    CHECK(!should_send_motion(sync, walking, net));
    // Standing with nothing to send: no faster sending.
    sync.since_sent = net.contact_send_interval_seconds + 0.001f;
    sync.unsent.clear();
    walking.speed = 0.0f;
    earlier.speed = 0.0f;
    walking.mode = motion_mode::idle;
    earlier.mode = motion_mode::idle;
    walking.position = earlier.position;
    sync.last_sent = earlier;
    CHECK(!should_send_motion(sync, walking, net));
}
