#pragma once

// The knocked-out golfers' bodies lying in this place, for the frame
// (renderer/lying_body.h): each from bodies_in_world (online the room's rows,
// offline the save's), on the ground under it, its bag beside it when it was
// on their back. Presentation only. GL-free.

#include "game/game_state.h"
#include "renderer/render_data.h"

void add_lying_bodies(render_data& data, const game_state& game);
