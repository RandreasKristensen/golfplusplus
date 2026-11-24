#pragma once

// Fills the frame's effect shelf (renderer/render_data.h render_effect_square)
// from what is running on me now, in either mode (current_effects in
// game/mode_dispatch.h), with the words resolved from the string table. The
// renderer lays it out and finds the hovered square. GL-free.

#include "game/game_state.h"
#include "game/text_assets.h"
#include "renderer/render_data.h"

void add_effect_shelf(render_data& data, const game_state& game, const text_assets& text);
