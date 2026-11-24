#pragma once

// When a contact is final. A flight is settled up to the earliest time every
// mover near its path has stepped to, and never short of the rewind window
// (server.contact_rewind_seconds): a mover's steps older than that are no
// longer accepted as news. A contact is applied once settled; of those found
// while waiting the earliest wins. Pure, shared with the server.

#include "game/ball_flight.h"
#include "game/game_tuning.h"

#include <cstdint>
#include <vector>

// Up to when (shared clock, ms) a flight's contacts are final, given the time
// each mover near it has stepped to: the earliest of them (and `now_ms`), but
// at least now minus the rewind window. With no mover near, now minus the window.
std::int64_t settled_until_ms(const std::vector<std::int64_t>& last_stepped_ms, std::int64_t now_ms, const server_tuning& tuning);

// Whether `contact`, which ends its step at the shared clock time
// `step_clock_us(flight.launch_ms, contact.step + 1)`, is at or before `settled_until_ms`.
bool contact_settled(const ball_flight& flight, const flight_contact& contact, std::int64_t settled_until_ms);

// The shared clock (ms) a scheduled check must run at to settle `contact`
// whatever the movers do: its time plus the rewind window, rounded up.
std::int64_t settle_due_ms(const ball_flight& flight, const flight_contact& contact, const server_tuning& tuning);

// What is pending once `found` is found: it replaces every pending contact
// when it is earlier than all of them (the later ones are dropped, their
// movers searched again after the flight is played on from it), else
// the pending are as they were and `found` is dropped. The same step goes to
// the one found first.
std::vector<flight_contact> pending_with(const std::vector<flight_contact>& pending, const flight_contact& found);
