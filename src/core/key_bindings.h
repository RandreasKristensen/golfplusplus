#pragma once

// The one place keys and the mouse are bound to game actions. The help
// screen text in assets/interface/text/en.json ("help.*") describes these bindings.

#include "core/input.h"
#include "game/game_input.h"

// `mouse_look_radians`: how far one count of mouse movement turns the look
// (0 while the mouse is not captured).
game_input game_input_from_keys(const input_state& keys, float mouse_look_radians);
