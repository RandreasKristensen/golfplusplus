#pragma once

// What the shadow map casts besides trees and floodlights: every figure (mine
// on foot too, which I never see) and every cart, built from the pieces they
// are drawn with, so the sun casts a golfer's shape and not a blob. Only their
// positions reach the map (renderer/shadow_map_pass.h). GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/render_data.h"
#include "renderer/world_marker_batch.h"

// Clears `batch` and fills it with this frame's casters.
void build_shadow_casters(world_marker_batch& batch, const render_data& data, const scene_colors& colors, const prop_set& props);
