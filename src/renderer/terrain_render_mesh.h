#pragma once

// Terrain meshes as one coloured, chunked render mesh (see terrain_palette.h
// for the colours). GL-free.

#include "game/lighting_loader.h"
#include "physics/ground_mesh.h"
#include "physics/terrain.h"
#include "renderer/render_mesh.h"
#include "renderer/sky_visibility.h"

#include <cstdint>
#include <vector>

// Appends `meshes` in order (null entries are skipped), colours each vertex by
// its material, gives it the sky it sees and its horizon from `light`
// (sky_visibility.h; empty grids: all of the sky, nothing on the horizon),
// and computes bounds and culling chunks.
render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, std::uint64_t revision,
                                            const ground_colors& colors, const ground_light_bake& light);
