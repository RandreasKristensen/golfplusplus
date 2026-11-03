#pragma once

// The putting reads of a frame (core/render_frame.h): the paces counted
// (game/pacing.h) popped up ahead of my eyes, and crouched on a green its
// slope's dots (game/green_read.h), kept off the cup and my ball. GL-free.

#include "game/game_state.h"
#include "game/text_assets.h"
#include "renderer/render_data.h"

// Needs data's ball and pin set first.
void add_putting_read(render_data& data, const game_state& game, const text_assets& text);
