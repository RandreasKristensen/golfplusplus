#pragma once

// Entering the free practice hub and teeing up on its optional shortcut mats.
#include "game/game_state.h"
bool start_hub(game_state& state, const hub_definition& hub);
bool tee_up_hub(game_state& state, std::size_t mat);
void finish_practice_shot(game_state& state);

