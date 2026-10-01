#pragma once

#include "core/input.h"

// Reads every pending SDL event into the next frame's input.
input_state poll_events(const input_state& previous);

// Starts/stops SDL text input. Enable it only while a text field is focused,
// so typing never doubles as game keys.
void set_text_input_enabled(bool enabled);
