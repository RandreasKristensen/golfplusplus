#pragma once

#include "core/input.h"

// Reads every pending SDL event into the next frame's input.
input_state poll_events(const input_state& previous);

// Hides the cursor and keeps it in the window, the mouse only moving
// (input_state::mouse_dx and mouse_dy), or gives the cursor back. SDL lets
// it go while the window is out of focus and takes it again on return.
void set_mouse_captured(bool captured);

// Starts/stops SDL text input. Enable it only while a text field is focused,
// so typing never doubles as game keys.
void set_text_input_enabled(bool enabled);
