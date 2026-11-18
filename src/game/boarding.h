#pragma once

// The bus at the hub's stop and what is heard of it: the starter's PA chimes
// and calls when a departure opens for boarding, the bus's doors stay open and
// its engine idles while it boards, and its horn sounds at the last call.
// Nothing is shown on the screen: out of earshot a golfer can miss it, as in
// real life. The cues follow the game clock the bookings use (server time
// online), never the renderer's.
//
// They are per departure, not per listener: a starter calls every group, so I
// hear the call for any group boarding as well as mine (the board's rows, or
// my own booking beyond them). Offline there is no clock: the bus is open for
// as long as I have a booking.
#include "game/game_state.h"

#include <cstdint>

// Where a departure at `scheduled` is `now` (UTC microseconds): boarding from
// `bus_early_minutes` before it, the last call `last_call_seconds` before it,
// gone from its time on.
boarding_phase boarding_phase_at(std::int64_t scheduled, std::int64_t now, const server_tuning& tuning);

// Whether the parked bus has its doors open and its engine idling: any
// departure I can hear is boarding, or offline I have a booking.
bool bus_doors_open(const game_state& state);

// Pushes the PA call and the horn for the departures that have reached their
// boarding or last call since the last frame, and keeps what it has seen.
// A departure first seen already boarding is not called again.
void update_boarding(game_state& state);
