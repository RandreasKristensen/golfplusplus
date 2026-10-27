#pragma once

// The swing, once Space commits the setup: the club goes back to the
// dialled backswing and down to the ball on the club's tempo, whatever the
// length, and the ball is struck when it gets there. No timing: an
// animation, not an input. Mine, and others' as shown (remote_players.h).

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
