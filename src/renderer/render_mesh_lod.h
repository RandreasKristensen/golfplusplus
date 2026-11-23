#pragma once

#include "game/lighting_loader.h"
#include "renderer/render_mesh.h"

#include <glm/mat4x4.hpp>

// GL-free coarser stand-ins for a chunked static mesh (the ground), made when
// a course loads and chosen per chunk by how far off they are on screen.
//
// Each chunk is thinned on its own by collapsing one free vertex at a time
// onto a neighbour, cheapest first, until the next collapse would pass the
// level's error. Level k allows `finest_error_meters * error_ratio^(k-1)`.
// What makes the levels safe to mix and to draw over the full mesh:
//   - a chunk's border vertices and edges never change, so neighbouring
//     chunks at different levels meet without a crack;
//   - a level is within its error of the full mesh (vertically, at the vertex a
//     collapse removes and where the new edges cross the old), and the error of
//     earlier collapses adds up; whatever lies on the full ground (the overlay,
//     grass, props) is at most that far off it, which a pass accepts as
//     sub-pixel and the overlay is lifted over (`lift_per_depth`);
//   - a vertex that differs in turf or blade kind from a neighbour is kept, so
//     material edges stay where the full mesh has them, and a collapse may move
//     the colour, normal and sky values by at most `attribute_error`, measured on
//     the values the GPU holds (renderer/terrain_vertex.h).
// Triangles are only ever dropped, never reordered or re-wound, so the full
// index buffer stays the oracle.

// Fills `mesh.lod_indices`, `lod_error_meters` and every chunk's `coarse`
// (nothing when the style has no levels or the mesh no chunks).
void build_render_mesh_lod(render_static_mesh& mesh, const terrain_lod_style& style);

// What a pass needs to pick a level: where the eye is, how many pixels a
// metre spans at one metre (`focal_pixels`), and the most error to accept.
struct render_lod_view {
    glm::vec3 eye{0.0f};
    float focal_pixels = 0.0f;
    float max_error_pixels = 0.0f;
    // How many metres per metre of view depth what lies on the full ground (the
    // material overlay) is lifted, so a coarse ground that rises over the full
    // one by what the pass accepts never buries it: the accepted error at the
    // chunk's nearest point, over the depth the widest corner of the picture
    // reaches. 0 when the full ground is drawn.
    float lift_per_depth = 0.0f;
};

// The view of a pass with projection `projection`, a picture `height` pixels
// tall, and `max_error_pixels` to accept (0: the full ground).
render_lod_view make_render_lod_view(const glm::vec3& eye, const glm::mat4& projection, int height, float max_error_pixels);

// The level to draw `chunk` at: 0 for the full mesh, else the coarsest built
// level whose error, at the chunk's nearest point to the eye, spans at most
// `view.max_error_pixels`.
std::size_t choose_chunk_lod_level(const render_mesh_chunk& chunk, const std::vector<float>& error_meters,
                                   const render_lod_view& view);
