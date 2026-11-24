#include "game/ball_flight.h"

#include <algorithm>

std::int64_t step_clock_us(const std::int64_t launch_ms, const int steps) {
    // steps / 120 s, in microseconds, rounded to the nearest.
    static_assert(shot_step_seconds == 1.0f / 120.0f, "step_clock_us counts a step as 1/120 s");
    return launch_ms * 1000 + (static_cast<std::int64_t>(steps) * 1000000 + 60) / 120;
}

namespace {
ball_flight flight_of(const flight_run& run, const std::int64_t launch_ms, const std::vector<flight_contact>& contacts,
                      const shot_course& course, const game_tuning& tuning) {
    ball_flight flight;
    flight.launch_ms = launch_ms;
    flight.checkpoints = run.checkpoints;
    flight.boxes = run.boxes;
    flight.contacts = contacts;
    flight.steps = run.cursor.steps;
    flight.open_from_step = run.contacts_end_step;
    flight.result = finish_run(run, course, tuning);
    return flight;
}
}

ball_flight fly_ball(const ball_state& launched, const std::int64_t launch_ms, const shot_course& course,
                     const game_tuning& tuning, const std::vector<flight_contact>& contacts) {
    const flight_run run = continue_run(start_run(launched, course, true), course, tuning, contacts);
    return flight_of(run, launch_ms, contacts, course, tuning);
}

ball_flight resume_flight(const ball_flight& flight, const int from_step, const std::vector<flight_contact>& contacts,
                          const shot_course& course, const game_tuning& tuning) {
    if (flight.checkpoints.empty()) {
        return flight;
    }
    const int start = std::clamp(from_step, 0, std::max(0, flight.steps - 1)) / flight_chunk_steps * flight_chunk_steps;
    const std::size_t checkpoint = static_cast<std::size_t>(start / flight_checkpoint_steps);
    const flight_checkpoint& at = flight.checkpoints[checkpoint];
    flight_run run;
    run.cursor = at.cursor;
    run.launch_position = flight.checkpoints.front().cursor.ball.position;
    run.recording = true;
    run.checkpoints.assign(flight.checkpoints.begin(), flight.checkpoints.begin() + static_cast<std::ptrdiff_t>(checkpoint));
    run.boxes.assign(flight.boxes.begin(), flight.boxes.begin() + start / flight_chunk_steps);
    run.result = flight.result;
    run.result.events.resize(at.event_count);
    run.result.person_hits.resize(at.hit_count);
    run.result.trajectory.resize(static_cast<std::size_t>(start / shot_steps_per_trajectory_point) + 1);
    run.contacts_end_step = flight.open_from_step <= start ? flight.open_from_step : 0;
    const flight_run flown = continue_run(std::move(run), course, tuning, contacts);
    ball_flight resumed = flight_of(flown, flight.launch_ms, contacts, course, tuning);
    resumed.result.strike_centredness = flight.result.strike_centredness;
    return resumed;
}

flight_cursor cursor_at(const ball_flight& flight, const int step, const shot_course& course, const game_tuning& tuning) {
    flight_cursor cursor = flight.checkpoints[static_cast<std::size_t>(step / flight_checkpoint_steps)].cursor;
    flight_stepper stepper(course, tuning, flight.contacts);
    while (cursor.steps < step) {
        cursor = stepper.advance(cursor).after;
    }
    return cursor;
}
