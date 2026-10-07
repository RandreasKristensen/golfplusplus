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
// `fov_degrees` is the player's field of view setting.
camera_view live_camera_view(const game_state& game, float fov_degrees);

// Where to draw a ball whose physical centre is `physics_center`. The drawn
// ball is bigger than the physical one, so it is raised to touch the ground
// where the physical ball does, never sunk into it.
glm::vec3 drawn_ball_center(const glm::vec3& physics_center, const world_scale_tuning& scale);

// The scene meshes the frame borrows (see render_data); app rebuilds them
// whenever game_state::terrain_render_revision changes.
struct render_meshes {
    const render_static_mesh* terrain = nullptr;  // the ground
    const render_static_mesh* material_overlay = nullptr;
    const render_hole_signs* hole_signs = nullptr;
    const render_fences* fences = nullptr;
    const render_water* water = nullptr;
};

// The in-round frame (no menus, no debug text). `keys` only drive the
// on-screen key icons; `fov_degrees` is the player's field of view setting.
render_data make_render_data(const game_state& game,
                             const input_state& keys,
                             const text_assets& text,
                             const std::vector<skill_definition>& skills,
                             const render_meshes& meshes,
                             float fov_degrees,
                             frame_profile* profile = nullptr);
