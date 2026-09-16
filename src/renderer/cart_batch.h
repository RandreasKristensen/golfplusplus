#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "renderer/world_marker_batch.h"

// GL-free construction of the golf cart model. The cart is 16 flat-colored,
// opaque, depth-writing pieces pinned to the camera basis: six panels on the
// unit quad, two detail cylinders, four canopy posts, and per wheel a cylinder
// plus a hub-cap sphere. It used to be 16 immediate-mode draws with five
// uniform sets each; it now pre-transforms into the shared world marker batch
// and rides along in that pass's depth-writing run.

// The renderer's old local_model(): builds the camera's yaw-only basis (right,
// up, forward), places `local` in it, then applies yaw, pitch, roll and scale
// in that order. Reproduced exactly, with the camera passed in rather than a
// render_data.
glm::mat4 camera_local_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const glm::vec3& rotation,
                             const glm::vec3& scale);

// The old local_point_world(): `local` placed in the same camera basis.
glm::vec3 camera_local_point(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local);

// The old draw_local_panel / draw_local_cylinder / draw_local_sphere model
// matrices. draw_local_cylinder scaled and then translated *after* the basis,
// so the composition order below is load bearing.
glm::mat4 local_panel_model(const glm::vec3& camera_position,
                            const glm::vec3& camera_target,
                            const glm::vec3& local,
                            const glm::vec3& rotation,
                            const glm::vec2& half_size);
glm::mat4 local_cylinder_model(const glm::vec3& camera_position,
                               const glm::vec3& camera_target,
                               const glm::vec3& local,
                               const glm::vec3& rotation,
                               const glm::vec3& scale);
glm::mat4 local_sphere_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             float radius);

// Appends the whole cart, in the order render_scene used to draw it. Appends
// nothing when the cart is inactive.
void append_cart_model(world_marker_batch& batch,
                       bool cart_active,
                       const glm::vec3& camera_position,
                       const glm::vec3& camera_target);

// Vertices the cart contributes when active: 6 quads, 8 cylinders, 2 spheres.
std::size_t cart_model_vertex_count();
