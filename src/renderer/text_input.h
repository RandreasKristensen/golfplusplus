#pragma once

// Single-line text field: pure state + a GL-free draw into the overlay batch.
// Typed characters come from input_state::text_typed (SDL_TEXTINPUT), which
// app only enables while a field is active, so typing never triggers game keys.

#include <cstddef>
#include <string>

#include "game/pixel_font_data.h"
#include "game/text_style.h"
#include "renderer/overlay_batch.h"
#include "renderer/ui_rect.h"

struct text_input_state {
    // UTF-8, like every on-screen string.
    std::string value;
    // In characters (code points), not bytes.
    std::size_t max_length = 12;
    // UTF-8 characters accepted as typed. Lowercase letters are accepted when
    // their uppercase form is listed, and stored uppercase (the font has no
    // lowercase).
    std::string allowed_chars;
    bool active = false;
};

// Returns the new state: `backspace` removes the last character first, then
// each typed character is appended if it is allowed and there is room.
// Inactive fields ignore input.
text_input_state apply_text_input(const text_input_state& state, const std::string& typed, bool backspace);

// Box with the value and, while active, a block cursor that blinks with
// `time_seconds`. `style` sets how the text fills `box`; its size is fixed
// for a full field so typing doesn't resize it.
void draw_text_input(overlay_batch& batch,
                     const pixel_font_data& font,
                     const text_style& style,
                     const text_input_state& state,
                     const ui_rect& box,
                     float time_seconds);
