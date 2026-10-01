#pragma once

// Terrain meshes as one coloured, chunked render mesh (see terrain_palette.h
// for the colours). GL-free.

#include "physics/terrain.h"
#include "renderer/render_mesh.h"

#include <cstdint>
#include <vector>

// Appends `meshes` in order (null entries are skipped), colours each vertex by
// its material, and computes bounds and culling chunks.
render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, std::uint64_t revision);
