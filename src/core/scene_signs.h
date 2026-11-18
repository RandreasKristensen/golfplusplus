#pragma once

// Keeps the scene's rendered signs (hole signs, way signs, the bus and tee book
// boards) current for the game: app calls it every frame, so what it decides
// is when the sign faces are painted again (renderer/hole_sign_batch.h).
// GL-free.

#include "core/startup_flow.h"
#include "game/game_state.h"
#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "game/text_assets.h"
#include "renderer/hole_sign_batch.h"

// Rebuilds `signs` when the course was loaded again (the terrain revision
// moved, so its ground colours and geometry may differ) or the bus board's
// destination or a clock's time changed; true when it did.
bool refresh_scene_hole_signs(render_hole_signs& signs,
                              const game_state& game,
                              const startup_catalog& catalog,
                              const text_assets& text,
                              const scene_colors& colors,
                              const prop_catalog& props,
                              const std::string& clock);
