#pragma once

// A flown shot kept so it can be resumed: the launch time on the shared clock,
// the ball's full state every flight_checkpoint_steps, a box round its path
// per flight_chunk_steps, the contacts played into it and its result. The
// server keeps one per ball in flight; a contact with a mover (ball_contact.h)
// resumes it from a checkpoint and flies it on, through the same stepping as
// simulate_ball (flight_cursor.h), so the result is exactly what flying it
// from the launch with the contacts gives. Pure, shared with the server.

#include "game/flight_cursor.h"
#include "game/shot_simulation.h"

#include <cstdint>
#include <vector>

struct ball_flight {
    std::int64_t launch_ms = 0;  // the shared clock at the launch
    std::vector<flight_checkpoint> checkpoints;  // checkpoint i is at step i * flight_checkpoint_steps
    std::vector<flight_box> boxes;               // box i covers steps i * flight_chunk_steps on
    // The contacts played in, in time order. A contact's solid is in play from
    // its step while the ball keeps striking it. A person hit through a contact
    // is recorded in `result.person_hits` with the contact's index here as its
    // entry; one on a person of the snapshot has that solid's index among the
    // flown course's dynamic solids.
    std::vector<flight_contact> contacts;
    int steps = 0;  // fixed steps flown
    // Contacts are looked for from this step on: the one after the last
    // contact's run.
    int open_from_step = 0;
    shot_result result;
};

// The shared clock `steps` steps after a launch at `launch_ms`, in microseconds
// (rounded to the nearest). Step number s (from 0) ends at step_clock_us(launch, s + 1).
std::int64_t step_clock_us(std::int64_t launch_ms, int steps);

// `launched` flown on `course` from `launch_ms`, with `contacts` played in.
// `result.strike_centredness` is 1: the caller sets it.
ball_flight fly_ball(const ball_state& launched, std::int64_t launch_ms, const shot_course& course, const game_tuning& tuning,
                     const std::vector<flight_contact>& contacts = {});

// `flight` flown again from the checkpoint that starts the chunk holding step
// `from_step`, with `contacts` (the flight's own and any later), keeping what
// came before. The same as fly_ball from the launch with the same contacts.
ball_flight resume_flight(const ball_flight& flight, int from_step, const std::vector<flight_contact>& contacts,
                          const shot_course& course, const game_tuning& tuning);

// The ball just before step `step` (0 up to flight.steps - 1), stepped from the
// nearest checkpoint with the flight's contacts.
flight_cursor cursor_at(const ball_flight& flight, int step, const shot_course& course, const game_tuning& tuning);
