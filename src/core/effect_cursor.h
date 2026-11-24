#pragma once

// The mouse freed to point at the effect shelf: holding the right button
// while walking on foot gives the cursor back (the view stops turning), and
// letting go captures it again. The decision and what the look does around it
// are pure, so tests cover them; app only carries the state and calls
// set_mouse_captured. GL-free.

#include "game/game_state.h"

// Whether the cursor is freed this frame: the right button is held, I am
// walking on foot in a round (not at address or in a swing, not at a cart's
// wheel or seat, not in a menu, the pause, the results or the hub's screens).
bool wants_effect_cursor(const game_state& game, bool right_held);

// The cursor across frames: freed, and for the frames after it is captured
// again, the mouse's counts still hold the free cursor's travel (some arrives
// between the last poll and the capture), never a look.
struct effect_cursor_state {
    bool freed = false;
    int settle_frames = 0;
};

effect_cursor_state next_effect_cursor(const effect_cursor_state& previous, bool wants);

// The mouse's counts must not turn the look this frame.
bool look_blocked(const effect_cursor_state& state);
