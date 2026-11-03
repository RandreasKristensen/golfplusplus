#pragma once

// The swing, once it is started over the ball: a swipe right takes the club
// back slowly, a meter rising towards the dialled backswing as its cap; a
// swipe left starts the downswing, the backswing it reached is the shot's,
// and starting down before the top or waiting there mistimes the strike.
// The ball is struck when the club gets down to it, on the club's tempo.
// Mine, and others' as shown (remote_players.h).

#include "game/club_definition.h"
#include "game/game_tuning.h"

struct swing_tempo {
    float back_seconds = 0.0f;
    float down_seconds = 0.0f;
};

swing_tempo swing_tempo_for(const club_definition& club, const swing_tuning& tuning);

// Seconds from the swing's start to impact.
float swing_impact_seconds(const swing_tempo& tempo);

// How far the club is raised `elapsed` seconds into a swing back to
// `backswing`: none at address, `backswing` at the top, none again at impact.
float swing_club_raise(float elapsed, float backswing, const swing_tempo& tempo);

// How far the meter has risen `elapsed` seconds after the swipe
// right, at one full backswing per meter_full_seconds, on past the dialled
// top while the player waits there.
float swing_meter_rise(float elapsed, const swing_tuning& tuning);

// How far that takes the club back, holding at `cap`.
float swing_meter_at(float elapsed, float cap, const swing_tuning& tuning);

// The strike's timing (shot_setup::strike_timing) for a downswing
// started `elapsed` seconds after the swipe right towards `cap`: on time
// within timing_window_seconds of the meter reaching it, else early or late
// by how much further, in full at timing_full_seconds.
float strike_timing_at(float elapsed, float cap, const swing_tuning& tuning);
