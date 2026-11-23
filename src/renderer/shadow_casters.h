#pragma once

// What casts into the sun's shadow map from the static world: every hole
// sign, way sign and fence post, as one list of triangles uploaded once per
// course and selected by tile (renderer/caster_index.h). Everything that moves
// casts from the frame's uploads instead: the figures and flagsticks are runs
// of the world markers' buffer (marker_view::sun_shadow), carts, bags and
// carried gear runs of the prop frame's (prop_view::sun_shadow), the placed
// models a range of the static prop buffer. Only positions reach the map
// (renderer/shadow_map_pass.h). GL-free.

#include "renderer/render_data.h"

#include <vector>

#include <glm/vec3.hpp>

// The corners (three per triangle) of every sign and fence post, which change
// only with the course.
std::vector<glm::vec3> static_caster_corners(const render_hole_signs* signs, const render_fences* fences);
