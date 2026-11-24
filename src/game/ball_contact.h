#pragma once

// A ball in flight meeting a mover (a golfer on foot or a cart) that was not
// where the swing's snapshot had it. The server plays a shot against what does
// not move, and finds contacts where movement is played: a mover's path over a
// span is compared with the flight (the chunk boxes first, then the ball's
// segment in each step at the mover's solid at that time) and a real
// contact is confirmed by stepping the ball once from the exact state with the
// mover's solid in the course, so the bounce is the shot code's. A contact is
// applied by flying the ball on from it with the static course; every client
// rebuilds the same flight from the shot's record and the contacts alone
// (replay_contacts). Pure, shared with the server.
//
// `flown_course` below is the course the flight was flown on: struck_course of
// the static course and the record, so its seed and snapshot are the shot's.
// Movers in its snapshot are met from the swing as ever.

#include "game/ball_flight.h"
#include "game/golfer.h"
#include "game/mover_path.h"
#include "game/shot_dice.h"

#include <cstdint>
#include <optional>
#include <vector>

// What a search did, for tests and tuning.
struct contact_search_stats {
    int chunks_tested = 0;       // chunks of the flight looked at
    int chunks_overlapping = 0;  // those whose box meets the path's in space and time
    int steps_tested = 0;        // shot steps whose segment met the mover's solid's bounds
    int steps_confirmed = 0;     // steps stepped again with the mover's solid
};

// The earliest step of `flight`, from flight.open_from_step, in which the ball
// meets the mover `path` describes, or none. The mover is the solid
// snapshot_solid makes of it at the time the step ends; the ball's segment
// over the step (and where its velocity alone would take it) is tested against
// that solid grown by two ball radii, and a hit confirmed by stepping. The
// shooter's own mover is met only after solids.self_hit_grace_seconds.
std::optional<flight_contact> first_contact(const ball_flight& flight, const mover_path& path, const shot_course& flown_course,
                                            const game_tuning& tuning, contact_search_stats* stats = nullptr);

// `flight` with `contact` played in: what came before unchanged, the contact
// step (and the steps the ball keeps striking the mover) with the mover's solid
// present, then the rest flown on the static course. The contact must be later
// than the flight's other contacts (its step at or after open_from_step).
ball_flight replay_after_contact(const ball_flight& flight, const flight_contact& contact, const shot_course& flown_course,
                                 const game_tuning& tuning);

// The shot as `golfer` hits it, from the swing, launched at record.setup.shot_at:
// no contacts. An unknown club leaves a flight of no steps.
ball_flight fly_struck_shot(const shot_record& record, const golfer_profile& golfer, const shot_course& course,
                            const game_tuning& tuning, const std::vector<club_definition>& clubs);

// The shot with `contacts` (in time order) applied, flown from the swing
// in one pass: what a client rebuilds from the shot's record and its contact
// rows. Equal, bit for bit, to fly_struck_shot followed by replay_after_contact
// for each.
ball_flight replay_contacts(const shot_record& record, const golfer_profile& golfer, const shot_course& course,
                            const game_tuning& tuning, const std::vector<club_definition>& clubs,
                            const std::vector<flight_contact>& contacts);
