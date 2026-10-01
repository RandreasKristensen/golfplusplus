#pragma once

// Builds the renderer's frame from game state: camera rigs, HUD state and
// marker lists. GL-free, so it is unit tested.

#include "core/input.h"
#include "game/game_state.h"
#include "game/reward_rules.h"
#include "game/text_assets.h"
#include "profiling/profiling.h"
#include "renderer/camera_transition.h"
#include "renderer/render_data.h"
#include "renderer/render_mesh.h"

#include <vector>

camera_rig active_camera_rig(const game_state& game);
// This frame's view of the active rig, before any transition blending.
camera_view live_camera_view(const game_state& game);

// The scene meshes the frame borrows (see render_data); app rebuilds them
// whenever game_state::terrain_render_revision changes.
struct render_meshes {
    const render_static_mesh* terrain = nullptr;  // terrain plus ground
    const render_static_mesh* material_overlay = nullptr;
};

// The in-round frame (no menus, no debug text). `keys` only drive the
// on-screen key icons.
render_data make_render_data(const game_state& game,
                             const input_state& keys,
                             const text_assets& text,
                             const std::vector<skill_definition>& skills,
                             const render_meshes& meshes,
                             frame_profile* profile = nullptr);
