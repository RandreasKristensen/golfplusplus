#pragma once

// The struck golfer's own game shows its own hit at once (presentation only).
// Online, the server finds a ball's contact with a golfer or cart once that
// mover's steps are settled, so its row reaches a client some time after the
// ball met them. A client knows every flight in its room (the shot's record and
// the contact rows so far) and its own exact steps, so it can see its own path
// meet a ball at once, replay that flight locally with the predicted contact and
// blend to it. The server's contact row for that flight replaces the
// prediction; none by the time the contact is settled and its row would be here
// retracts it, never to be predicted again for that flight. Effects (dazed,
// knockout) never start from a prediction: they come only from rows. Pure.

#include "game/ball_contact.h"
#include "game/ball_flight.h"
#include "game/game_tuning.h"
#include "game/mover_path.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

// My own steps over the last moments, as the server will have them from my
// movement: my feet after each step on foot, or my cart's pose when I drive.
struct my_path_log {
    snapshot_kind kind = snapshot_kind::person;
    std::vector<mover_sample> samples;
};

// Adds where I stand (feet or cart) at `sample.time_ms`, when it differs from the
// last sample; a change of `kind` starts the log afresh. Samples before
// `keep_from_ms` go, but for the last of them (it is held up to the next).
void note_path_sample(my_path_log& log, snapshot_kind kind, const mover_sample& sample, std::int64_t keep_from_ms);

// The logged path as `mover` (who I am, `shooter` when the flight is my own
// shot), held up to `now_ms`: what I have stepped so far, never a guess ahead.
mover_path logged_path(const my_path_log& log, const snapshot_entry& mover, std::int64_t now_ms);

// What this client has worked out about one flight that is playing here, beyond
// the server's contact rows.
struct contact_watch {
    // The flight as the rows have it (the shot with the contacts so far),
    // built when a mover first comes near, rebuilt when a contact row joins it.
    std::optional<ball_flight> flight;
    std::size_t flight_contacts = 0;  // the contacts it was built with
    std::optional<flight_contact> predicted;  // my own, found from my steps
    glm::vec3 correction_before{0.0f};  // the shot's correction before it was predicted
    bool given_up = false;  // a prediction the server never confirmed: not again
};

// Whether the shot's trajectory passes within `reach` (world units) of `position`
// in the ground plane: cheap, before the flight is built.
bool trajectory_near(const shot_result& result, const glm::vec3& position, float reach);

// Whether the flight's boxes for the time from `from_ms` on come within `reach`
// (world units) of `position`.
bool flight_near(const ball_flight& flight, const glm::vec3& position, float reach, std::int64_t from_ms);

// Whether a prediction at `contact` is overdue its row at `now_ms`: its time
// and net.contact_prediction_hold_seconds (the rewind window it settles in and
// the row's way here) have passed.
bool prediction_overdue(const ball_flight& flight, const flight_contact& contact, std::int64_t now_ms, const net_tuning& tuning);
