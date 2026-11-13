#pragma once

// What the shadow map casts besides trees and floodlights: every figure (mine
// on foot too, which I never see) with what it carries, every cart and every
// bag, every flagstick with its flag, standing or lying, and every hole sign,
// way sign and fence post near enough to fall on the map, built from the
// pieces they are drawn with, so the sun casts a golfer's shape and not a
// blob. Only their positions reach the map (renderer/shadow_map_pass.h).
// GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"
#include "renderer/render_data.h"
#include "renderer/world_marker_batch.h"

// Clears both and fills them with this frame's casters: the figures,
// flagsticks, signs and fence posts, and the carts' models (their models
// from `props`). A flagstick's pole is square to the sun and at least one of
// the map's texels (`map`) wide, so the map catches it; signs and posts
// further from the camera than the map reaches are left out.
void build_shadow_casters(world_marker_batch& figures, prop_batch& models, const render_data& data, const scene_colors& colors,
                          const prop_catalog& props, const shadow_style& map, const prop_batch* static_models = nullptr);
