#pragma once

// Placement in the camera's yaw-only frame (right, up, forward), for things
// held in front of the camera: the golf cart and the emote props. GL-free.

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// `local` (x right, y up, z forward) in world space.
glm::vec3 camera_local_point(const glm::vec3& camera_position, const glm::vec3& camera_target, const glm::vec3& local);

// The camera frame at `local`, then yaw (rotation.y), pitch (x), roll (z), then scale.
glm::mat4 camera_local_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const glm::vec3& rotation,
                             const glm::vec3& scale);

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
