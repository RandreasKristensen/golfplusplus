#pragma once

// The one place keys are bound to game actions. The help screen text in
// assets/text/en.json ("help.*") describes these bindings.

#include "core/input.h"
#include "game/game_input.h"

game_input game_input_from_keys(const input_state& keys);
