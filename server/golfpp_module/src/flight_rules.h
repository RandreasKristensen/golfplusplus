#pragma once

// A shot in flight as the server keeps it, and what finds its contacts with
// golfers and carts (game/ball_contact.h) where their movement is played.
// At the swing the ball is flown against what does not move and every mover
// standing where it is now (provisional: the earliest such contact is
// pending). A mover's steps then replace what is known of it, a contact is
// applied once settled (game/contact_settle.h), and the flight is final at
// its rest plus the rewind window. Plain C++ for the reducers and the native
// tests; the rows are in flight_schema.h.

#include "game/ball_contact.h"
#include "game/ball_hit_rules.h"
#include "game/contact_settle.h"
#include "game/golfer.h"
#include "game/mover_path.h"

#include <cstdint>
#include <vector>

// Everything about a shot the server needs again after the swing.
struct live_flight {
    shot_record record;  // the setup, the dice and the snapshot of what does not move
    golfer_profile golfer;
    int hole = 0;
    int stroke = 0;  // the stroke this shot is, counted at the swing
    std::vector<int> flags_out;
    ball_flight flight;
    // Zero or one: the earliest contact found and not yet settled.
    std::vector<flight_contact> pending;
    // Persons whose first contact of this flight was rolled: only that one
    // has an outcome, later ones just bounce.
    std::vector<std::uint64_t> rolled;
};

// What a flight is played on and with.
struct flight_env {
    const shot_course& course;  // the static course as of the shot (no snapshot, no movers)
    const game_tuning& tuning;
    const std::vector<club_definition>& clubs;
};

// A mover as the server knows it: standing where `entry` says from `since_ms`
// (what its steps have told up to then is not asked again).
struct known_mover {
    snapshot_entry entry;
    std::int64_t since_ms = 0;
};

// Whether two entries are the same mover (a golfer, or a cart by its owner).
bool same_mover(const snapshot_entry& a, const snapshot_entry& b);

// The shared clock (ms, rounded up) when the flight's ball rests.
std::int64_t flight_end_ms(const ball_flight& flight);

// The shot flown from the swing on the static course with `record`'s snapshot,
// and the earliest contact of `movers` standing where they are.
live_flight begin_flight(const shot_record& record, const golfer_profile& golfer, int hole, int stroke,
                         const std::vector<int>& flags_out, const std::vector<known_mover>& movers, const flight_env& env);

// The contact of `live` with a mover that has played `path` (its steps, then
// where the last leaves it held to the flight's end): pending when it is the
// earliest. When the pending contact was this mover's own and the path now
// takes it elsewhere, `others` (every other mover, standing) are searched again.
// `gone`: the mover is no longer there after its last step (a golfer got into a cart).
live_flight flight_after_movement(live_flight live, mover_path path, bool gone, const std::vector<known_mover>& others,
                                  const flight_env& env);

// A contact applied to a flight: its index among the flight's contacts and
// the outcome rolled for the mover it met (none for a cart, for a mover
// already rolled and for a ball too slow to matter).
struct applied_contact {
    flight_contact contact;
    int index = 0;
    hit_outcome_kind outcome = hit_outcome_kind::none;
};

struct settlement {
    live_flight live;
    std::vector<applied_contact> applied;
};

// Applies the pending contact while it is settled by `settled_until_ms`, the
// flight playing on from each (and `movers` searched again, the one just met
// standing where it met the ball).
settlement settle_flight(live_flight live, std::int64_t settled_until_ms, const std::vector<known_mover>& movers,
                         const flight_env& env);

// When the flight is final: its rest and the rewind window.
std::int64_t landing_due_ms(const live_flight& live, const server_tuning& tuning);

// When the server must next look at the flight: the pending contact's
// settle or the landing, the earlier.
std::int64_t next_due_ms(const live_flight& live, const server_tuning& tuning);

// The boxes of a flight as plain numbers (six a chunk: low, high), so a
// mover can be ruled out without decoding the flight.
std::vector<float> flight_box_values(const ball_flight& flight);

// The times (shared clock, ms) up to which the movers within `reach` of the
// flight's boxes have stepped, for settled_until_ms; movers with no steps
// timed are left out.
std::vector<std::int64_t> stepped_times_near(const std::vector<float>& boxes, const std::vector<known_mover>& movers, float reach);

// Whether `path` may meet a flight with these boxes (at least one chunk's
// box overlaps the path's swept box in its time).
bool path_near_flight(const std::vector<float>& boxes, std::int64_t launch_ms, const mover_path& path, const game_tuning& tuning);
