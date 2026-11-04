#pragma once

// The cart paths (game/cart_path.h) drawn with the ground: the ground's own
// triangles cut to each path's footprint (its stretches and the round ends
// that fill its bends and junctions, as its slabs are), every vertex at the
// walkable top the game stands on there (sample_area). So the drawn path lies
// exactly on the ground as played, its lift over it, with no gap at a join
// and nothing under the ground between vertices. Built with the course's
// static meshes and drawn in the material overlay's pass. GL-free.

#include "game/play_area.h"
#include "renderer/render_mesh.h"
#include "renderer/sky_visibility.h"

#include <glm/vec3.hpp>

// `mesh` with `area`'s cart paths appended, its bounds and chunks recomputed.
render_static_mesh with_cart_paths(render_static_mesh mesh, const play_area& area, const glm::vec3& color,
                                   const ground_light_bake& light);
