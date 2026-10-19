#pragma once

// How much tracking noise the tape shows (renderer/tape_pass.h): while the
// game is paused (the pause menu, a finished round's results) and for a
// moment after a hole starts or ends, never while aiming, addressing or
// following a shot. Looks only; nothing in the game reads it. GL-free.

#include "game/game_state.h"
#include "game/picture_loader.h"

struct tape_tracking_state {
    float level = 0.0f;       // 0..1, what the frame shows
    float burst_left = 0.0f;  // seconds of the hole-change burst still to run
    bool on_hole = false;     // a hole was being played last frame
};

struct tape_tracking_signals {
    bool paused = false;
    bool on_hole = false;
    bool in_play = false;  // aiming, addressing or following a shot
};

// `paused`: the pause menu or the round's results are up.
tape_tracking_signals tape_tracking_signals_for(const game_state& game, bool paused);

// Eases towards full while paused or bursting, over tape.tracking_fade_seconds,
// and cuts to none in play. `dt` is real seconds.
tape_tracking_state update_tape_tracking(const tape_tracking_state& state, const tape_tracking_signals& signals, float dt,
                                         const tape_style& tape);
