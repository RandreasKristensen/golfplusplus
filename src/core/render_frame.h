#pragma once

// Builds the renderer's frame from game state: camera rigs, HUD state and
// marker lists. GL-free, so it is unit tested.

#include "core/render_gear.h"
#include "game/game_state.h"
#include "game/reward_rules.h"
#include "game/text_assets.h"
#include "physics/cup.h"
#include "profiling/profiling.h"
#include "renderer/camera_transition.h"
#include "renderer/course_map_sheet.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/render_data.h"
#include "renderer/render_mesh.h"

#include <optional>
#include <string>
#include <vector>

camera_rig active_camera_rig(const game_state& game);
// This frame's view of the active rig, before any transition blending.
// `fov_degrees` is the player's field of view setting.
camera_view live_camera_view(const game_state& game, float fov_degrees);

// Where a ball whose physics centre is `physics_center` is drawn: right
// over it, raised so the bigger drawn ball sits on the ground where the
// physical one does. The cup is drawn as big as it plays, so a putt is
// drawn rolling, lipping and dropping exactly as it does.
glm::vec3 drawn_ball_center(const glm::vec3& physics_center, const world_scale_tuning& scale);

// The same, near `cups`: once the drawn ball sinks below a rim it is moved
// in towards the hole's axis just enough that what is below the rim stays
// inside the wall, as the smaller real ball does.
glm::vec3 drawn_ball_center(const glm::vec3& physics_center, const world_scale_tuning& scale, const std::vector<cup>& cups);

// The club in hand drawn at `setup`'s ball, swung back `raise`: its face
// turned and its shaft leaning as the setup has them (game/delivery.h), its
// head where `club_model` has it. None when `props` lacks the model.
std::optional<render_club_pose> club_pose_at(const game_state& game, const shot_setup& setup, float raise,
                                             const std::string& club_model, const prop_catalog& props);

// `line` drawn, at its fade, once it has a segment.
void add_flight_line(render_data& data, const flight_line& line, const flight_path_tuning& tuning);

// What each sign of the play area says (area.signs order): its course hole,
// par and length; a practice ground's bay, its target's distance.
std::vector<hole_sign_text> hole_sign_texts(const game_state& game);

// The scene meshes the frame borrows (see render_data); app rebuilds them
// whenever game_state::terrain_render_revision changes.
struct render_meshes {
    const render_static_mesh* terrain = nullptr;  // the ground
    const render_static_mesh* material_overlay = nullptr;
    const render_hole_signs* hole_signs = nullptr;
    const render_fences* fences = nullptr;
    const render_water* water = nullptr;
    const render_floodlights* floodlights = nullptr;
    const grass_field* grass = nullptr;
    const course_map_sheet* course_map = nullptr;  // the paper map's ground
};

// The in-round frame (no menus, no debug text); `fov_degrees` is the
// player's field of view setting, `interact_prompt` its interact prompt one.
// `cart_fits` carries the carts' ground fits from one frame to the next; none
// (a test's or a still's) fits every cart afresh. `scripted_carts` are the
// benchmark's crowd (see add_carts_and_bags); none in a real frame.
render_data make_render_data(const game_state& game,
                             const text_assets& text,
                             const skill_catalog& skills,
                             const render_meshes& meshes,
                             const prop_catalog& props,
                             float fov_degrees,
                             bool interact_prompt,
                             cart_fit_memo* cart_fits = nullptr,
                             const std::vector<cart_in_world>& scripted_carts = {});
