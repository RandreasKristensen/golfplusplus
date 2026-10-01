#include "renderer/camera_local.h"

#include "physics/vector_math.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec4.hpp>

namespace {
glm::vec3 camera_forward(const glm::vec3& camera_position, const glm::vec3& camera_target) {
    return safe_normalize(horizontal(camera_target - camera_position), glm::vec3(0.0f, 0.0f, 1.0f));
}

glm::vec3 camera_right(const glm::vec3& forward) {
    return glm::normalize(glm::cross(world_up, forward));
}
}

glm::vec3 camera_local_point(const glm::vec3& camera_position, const glm::vec3& camera_target, const glm::vec3& local) {
    const glm::vec3 forward = camera_forward(camera_position, camera_target);
    return camera_position + camera_right(forward) * local.x + world_up * local.y + forward * local.z;
}

glm::mat4 camera_local_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const glm::vec3& rotation,
                             const glm::vec3& scale) {
    const glm::vec3 forward = camera_forward(camera_position, camera_target);
    glm::mat4 model(1.0f);
    model[0] = glm::vec4(camera_right(forward), 0.0f);
    model[1] = glm::vec4(world_up, 0.0f);
    model[2] = glm::vec4(forward, 0.0f);
    model[3] = glm::vec4(camera_local_point(camera_position, camera_target, local), 1.0f);
    model = glm::rotate(model, rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::rotate(model, rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, scale);
}

glm::mat4 local_panel_model(const glm::vec3& camera_position,
                            const glm::vec3& camera_target,
                            const glm::vec3& local,
                            const glm::vec3& rotation,
                            const glm::vec2& half_size) {
    return camera_local_model(camera_position, camera_target, local, rotation, glm::vec3(half_size, 1.0f));
}

glm::mat4 local_cylinder_model(const glm::vec3& camera_position,
                               const glm::vec3& camera_target,
                               const glm::vec3& local,
                               const glm::vec3& rotation,
                               const glm::vec3& scale) {
    // Scale first, then shift half a (scaled) unit down so the cylinder is
    // centred on `local`.
    const glm::mat4 model = glm::scale(camera_local_model(camera_position, camera_target, local, rotation, glm::vec3(1.0f)), scale);
    return glm::translate(model, glm::vec3(0.0f, -0.5f, 0.0f));
}

glm::mat4 local_sphere_model(const glm::vec3& camera_position,
                             const glm::vec3& camera_target,
                             const glm::vec3& local,
                             const float radius) {
    return camera_local_model(camera_position, camera_target, local, glm::vec3(0.0f), glm::vec3(radius));
}

glm::mat4 local_segment_model(const glm::vec3& camera_position,
                              const glm::vec3& camera_target,
                              const glm::vec3& start_local,
                              const glm::vec3& end_local,
                              const float radius) {
    const glm::vec3 start = camera_local_point(camera_position, camera_target, start_local);
    const glm::vec3 axis = camera_local_point(camera_position, camera_target, end_local) - start;
    const float length = glm::length(axis);
    if (length <= 0.0001f) {
        return glm::mat4(0.0f);
    }

    const glm::vec3 axis_direction = axis / length;
    const glm::vec3 view_direction = glm::normalize(camera_target - camera_position);
    const glm::vec3 reference = std::abs(glm::dot(axis_direction, view_direction)) > 0.92f ? world_up : view_direction;
    const glm::vec3 side = glm::normalize(glm::cross(axis_direction, reference));
    const glm::vec3 bend = glm::normalize(glm::cross(side, axis_direction));

    glm::mat4 model(1.0f);
    model[0] = glm::vec4(side * radius, 0.0f);
    model[1] = glm::vec4(axis, 0.0f);
    model[2] = glm::vec4(bend * radius, 0.0f);
    model[3] = glm::vec4(start, 1.0f);
    return model;
}
