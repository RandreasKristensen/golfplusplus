#pragma once

struct input_state;

void poll_events(input_state& input);

// Starts/stops SDL text input. Enable it only while a text field is focused,
// so typing never doubles as game keys.
void set_text_input_enabled(bool enabled);
