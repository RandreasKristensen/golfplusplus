#pragma once

// The golf cart, built into the world marker batch. It is pinned to the
// camera (see camera_local.h): 16 flat-coloured, opaque pieces. GL-free.

#include "renderer/world_marker_batch.h"

#include <cstddef>

#include <glm/vec3.hpp>

// Appends nothing when the cart is inactive.
void append_cart_model(world_marker_batch& batch,
                       bool cart_active,
                       const glm::vec3& camera_position,
                       const glm::vec3& camera_target);

// Vertices the cart contributes when active: 6 quads, 8 cylinders, 2 spheres.
std::size_t cart_model_vertex_count();
