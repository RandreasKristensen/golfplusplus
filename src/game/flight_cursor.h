#pragma once

// A ball in flight as a resumable run: the one place a shot is stepped.
// simulate_ball, a stored flight (ball_flight.h) and the search for a mover
// the ball met (ball_contact.h) all step through advance_flight, so a flight
// resumed from a checkpoint reproduces the original bit for bit. A cursor
// holds everything the next step depends on. Pure, shared with the server.

#include "game/game_tuning.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "physics/solid.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// Where a ball first came over a cup it went into.
struct cup_entry {
    std::size_t cup = 0;
    glm::vec3 position{0.0f};
};

// A flight between two steps. `ground` is always sample_area at `ball`'s
// position, kept so a step need not sample it twice.
struct flight_cursor {
    ball_state ball;
    terrain_sample ground;
    int steps = 0;  // fixed steps flown
    bool from_green = false;  // it began on a putting green
    bool hit_ball_on_green = false;
    bool lost = false;
    bool in_leaves = false;
    bool finished = false;  // rested, plugged or sunk
    int on_pond_floor = -1;  // steps since a lost ball reached the floor
    std::optional<shot_event_kind> touching;  // what it struck last step
    std::optional<std::size_t> touching_person;
    std::optional<std::size_t> touching_cup;
    std::vector<cup_entry> came_in;  // the latest entry per cup it touched
    std::optional<std::size_t> resting_cup;
    // The contact (of the list the flight is played with) whose mover's solid
    // is present for the next step: the ball met it in the step before.
    std::optional<std::size_t> in_contact;
};

// A ball meeting a mover (a golfer on foot or a cart) that was not in the
// shot's snapshot: found by the server where movement is played, applied
// once settled, and replayed by every client from this alone (ball_contact.h).
struct flight_contact {
    int step = 0;  // the shot step the ball met it in (0 is the first after the launch)
    // Where the mover stood in that step, its kind, who (`account`: a golfer's,
    // or a cart's owner's) and whether it is the one who hit the shot.
    snapshot_entry mover;
    float time = 0.0f;  // seconds after launch, the end of that step
    float speed_mps = 0.0f;  // the ball's, just before the step
    float height_m = 0.0f;  // of the ball's centre above the mover's base, within its height
};

// The mover's solid as the shot meets it (the one snapshot_solids makes).
solid contact_solid(const flight_contact& contact, const game_tuning& tuning);

// The cursor of `launched` at the start of the flight.
flight_cursor start_flight(const ball_state& launched, const shot_course& course);

struct flight_advance {
    flight_cursor after;
    std::array<shot_event, 4> events;  // in time order: land, hit, leaves, cup_rattle
    int event_count = 0;
    std::optional<person_hit> hit;
    std::optional<std::size_t> hit_dynamic;  // which of the course's dynamic solids the ball struck
};

// One fixed step from `cursor` on `course`.
flight_advance advance_flight(const flight_cursor& cursor, const shot_course& course, const game_tuning& tuning);

// Steps a flight along a course that does not move, with `contacts` (in time
// order, not overlapping) played in: for the steps a contact is in play its
// mover's solid is the course's last dynamic solid. A contact starts at its
// `step` and lasts while the ball keeps striking its solid. Hits met in a
// contact's solid are recorded with its index in `contacts` as their entry.
class flight_stepper {
public:
    flight_stepper(const shot_course& course, const game_tuning& tuning, const std::vector<flight_contact>& contacts);

    flight_advance advance(const flight_cursor& cursor);

private:
    const shot_course& course_;
    const game_tuning& tuning_;
    const std::vector<flight_contact>& contacts_;
    std::optional<std::size_t> built_for_;
    std::optional<shot_course> with_mover_;
};

// The ball's state every flight_checkpoint_steps, with how much of the
// result there was by then, so a flight resumes from it.
struct flight_checkpoint {
    flight_cursor cursor;
    std::uint32_t event_count = 0;
    std::uint32_t hit_count = 0;
};

// A box enclosing the ball's path over flight_chunk_steps steps.
struct flight_box {
    glm::vec3 low{0.0f};
    glm::vec3 high{0.0f};
};

// A flight on its way: the cursor, the result so far (not yet finished) and,
// when recording, its checkpoints and chunk boxes.
struct flight_run {
    flight_cursor cursor;
    shot_result result;
    glm::vec3 launch_position{0.0f};
    // The step after the last step the ball struck a contact's mover in.
    int contacts_end_step = 0;
    bool recording = false;
    std::vector<flight_checkpoint> checkpoints;
    std::vector<flight_box> boxes;
    std::optional<flight_box> open_box;
};

flight_run start_run(const ball_state& launched, const shot_course& course, bool recording);

// Flies `run` on until the ball rests or the shot is as long as it may be,
// with `contacts` played in (flight_stepper). A recording run must begin on a
// multiple of flight_chunk_steps.
flight_run continue_run(flight_run run, const shot_course& course, const game_tuning& tuning,
                        const std::vector<flight_contact>& contacts);

// The finished shot: its last point, duration, rest, whether holed or lost
// and the penalty.
shot_result finish_run(const flight_run& run, const shot_course& course, const game_tuning& tuning);
