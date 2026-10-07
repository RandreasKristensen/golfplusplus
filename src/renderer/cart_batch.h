#pragma once

// The golf cart, built into the world marker batch from flat-coloured,
// opaque pieces: mine as seen from the driver's seat, pinned to the camera
// (see camera_local.h), only its front in view; another player's whole, on
// the ground around its driver (remote_avatar_batch.h). Both have the
// steering wheel in the same place before the driver. GL-free.

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

// The whole cart, its driver's feet at `feet` (on the ground under the
// seat), facing along `yaw` (see yaw_direction in physics/vector_math.h).
void append_outside_cart_model(world_marker_batch& batch, const glm::vec3& feet, float yaw);
std::size_t outside_cart_vertex_count();
