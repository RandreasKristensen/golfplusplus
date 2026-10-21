#pragma once

// Placement in a yaw-only frame at a point facing a target, a thing's own,
// for golf carts, figures and the emote props they hold; and the camera's
// whole frame, pitch included, for the props pinned to my view. GL-free.
//
// Every such frame is x left, y up, z forward (x = yaw_left of z): a plain
// rotation of the world, so turns follow the right-hand rule. Seen from
// behind, +x is on the left of the screen. Pose a thing by where its sides
// point (posed_model), not by angles, where you can.

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// `local` (x left, y up, z forward) in world space.
glm::vec3 camera_local_point(const glm::vec3& camera_position, const glm::vec3& camera_target, const glm::vec3& local);

// The camera's whole frame at its position, pitched with its look (x left,
// y up, z forward, as the yaw-only frame at no pitch): what is pinned to the view.
glm::mat4 camera_view_frame(const glm::vec3& camera_position, const glm::vec3& camera_target);

// The camera frame at `local`, then yaw (rotation.y), pitch (x), roll (z),
// then scale. A positive yaw turns forward towards the left, a positive
// pitch tips the top forward, a positive roll tips the top to the right.
glm::mat4 camera_local_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const glm::vec3& rotation,
                             const glm::vec3& scale);

// A thing's own frame in a frame like the above, at `at`: its +y along `up`
// and its +z towards `front` (squared to `up`), then scaled.
glm::mat4 posed_model(const glm::vec3& at, const glm::vec3& up, const glm::vec3& front, const glm::vec3& scale);

// Unit quad (XY, [-1, 1]) scaled to `half_size`.
glm::mat4 local_panel_model(const glm::vec3& camera_position,
                            const glm::vec3& camera_target,
                            const glm::vec3& local,
                            const glm::vec3& rotation,
                            const glm::vec2& half_size);
// Unit cylinder (y in [0, 1]) scaled, then centred on `local`.
glm::mat4 local_cylinder_model(const glm::vec3& camera_position,
                               const glm::vec3& camera_target,
                               const glm::vec3& local,
                               const glm::vec3& rotation,
                               const glm::vec3& scale);
// Unit sphere of `radius` at `local`.
glm::mat4 local_sphere_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             float radius);
// Unit cylinder stretched from `start_local` to `end_local` with `radius`,
// turned to face the camera.
glm::mat4 local_segment_model(const glm::vec3& camera_position,
                              const glm::vec3& camera_target,
                              const glm::vec3& start_local,
                              const glm::vec3& end_local,
                              float radius);
