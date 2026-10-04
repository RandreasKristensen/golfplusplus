#pragma once

// The online parts of a frame (core/render_frame.h): other players, their
// balls, shot trails and name tags, the group's scorecard rows and the
// server's notices. Nothing is added offline. GL-free.

#include "game/game_state.h"
#include "game/text_assets.h"
#include "renderer/render_data.h"

void add_online_render_data(render_data& data, const game_state& game, const text_assets& text);
