#pragma once

// Terrain meshes as one coloured, chunked render mesh (see terrain_palette.h
// for the colours). GL-free.

#include "game/lighting_loader.h"
#include "game/play_area.h"
#include "physics/ground_mesh.h"
#include "physics/terrain.h"
#include "renderer/render_mesh.h"
#include "renderer/sky_visibility.h"

#include <cstdint>
#include <vector>

// A vertex of the ground or of what lies flat on it: its colour, its grass
// texture's share (`rough`), the sky it sees and its horizon from `light`
// (sky_visibility.h; empty grids: all of the sky, nothing on the horizon).
render_terrain_vertex lit_ground_vertex(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& color,
                                        float rough, const ground_light_bake& light);

// Appends `meshes` in order (null entries are skipped), colours each vertex by
// its material, gives it the sky it sees and its horizon from `light`
// (sky_visibility.h; empty grids: all of the sky, nothing on the horizon),
// and computes bounds and culling chunks. Under a pond of `ponds` (none when
// null) the bank's grass turns to the mud of its floor just inside its edge.
render_static_mesh make_terrain_render_mesh(const std::vector<const terrain_mesh*>& meshes, std::uint64_t revision,
                                            const ground_colors& colors, const ground_light_bake& light,
                                            const play_area* ponds);

// `area`'s ground as make_terrain_render_mesh draws it, under its zones'
// overlay, which alone draws their shapes: each vertex in what sample_area
// finds there with no zone (its holes' material, the fairway a ribbon
// paints, else rough), so no zone's colour or bareness bleeds past its
// outline across the ground's cells. A pond's bed stays the pond's. It carries
// the coarser levels `lod` asks for (renderer/render_mesh_lod.h).
render_static_mesh make_ground_render_mesh(const play_area& area, std::uint64_t revision, const ground_colors& colors,
                                           const ground_light_bake& light, const terrain_lod_style& lod);
