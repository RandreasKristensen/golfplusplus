#include "renderer/prop_pieces.h"

#include "renderer/camera_local.h"

#include <cstddef>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The camera_local helpers place in a yaw-only frame at a point facing a
// target; at the origin facing +z that frame is the prop's own.
const glm::vec3 origin(0.0f);
const glm::vec3 ahead(0.0f, 0.0f, 1.0f);

constexpr std::size_t box_faces = 6;
}

std::vector<prop_piece> prop_pieces(const prop_model& model, const glm::mat4& frame) {
    std::vector<prop_piece> pieces;
    for (const prop_part& part : model.parts) {
        const glm::vec3 turn = glm::radians(part.turn_degrees);
        switch (part.shape) {
        case prop_shape::cylinder:
            pieces.push_back(prop_piece{prop_mesh::cylinder,
                                        frame * local_segment_model(origin, ahead, part.from, part.to, part.radius),
                                        part.color, &part});
            break;
        case prop_shape::sphere:
            pieces.push_back(prop_piece{prop_mesh::sphere, frame * local_sphere_model(origin, ahead, part.at, part.radius),
                                        part.color, &part});
            break;
        case prop_shape::panel:
            pieces.push_back(prop_piece{prop_mesh::quad,
                                        frame * local_panel_model(origin, ahead, part.at, turn, glm::vec2(part.size) * 0.5f),
                                        part.color, &part});
            break;
        case prop_shape::box: {
            // Each face a quad in the box's own turned frame.
            const glm::mat4 box = frame * camera_local_model(origin, ahead, part.at, turn, glm::vec3(1.0f));
            const glm::vec3 half = part.size * 0.5f;
            const float quarter = glm::radians(90.0f);
            // `axis`: what the unit quad (facing z) turns a quarter about to face its side.
            const auto face = [&](const glm::vec3& centre, const glm::vec3& axis, const glm::vec2& face_half) {
                glm::mat4 quad = glm::translate(glm::mat4(1.0f), centre);
                if (axis != glm::vec3(0.0f)) {
                    quad = glm::rotate(quad, quarter, axis);
                }
                pieces.push_back(prop_piece{prop_mesh::quad, box * glm::scale(quad, glm::vec3(face_half, 1.0f)), part.color, &part});
            };
            const glm::vec3 none(0.0f);
            const glm::vec3 x_axis(1.0f, 0.0f, 0.0f);
            const glm::vec3 y_axis(0.0f, 1.0f, 0.0f);
            for (const float side : {-1.0f, 1.0f}) {
                face(glm::vec3(0.0f, 0.0f, half.z * side), none, glm::vec2(half.x, half.y));
                face(glm::vec3(half.x * side, 0.0f, 0.0f), y_axis, glm::vec2(half.z, half.y));
                face(glm::vec3(0.0f, half.y * side, 0.0f), x_axis, glm::vec2(half.x, half.z));
            }
            break;
        }
        }
    }
    return pieces;
}

std::size_t prop_piece_count(const prop_model& model, const prop_mesh mesh) {
    std::size_t count = 0;
    for (const prop_part& part : model.parts) {
        switch (part.shape) {
        case prop_shape::cylinder:
            count += mesh == prop_mesh::cylinder ? 1U : 0U;
            break;
        case prop_shape::sphere:
            count += mesh == prop_mesh::sphere ? 1U : 0U;
            break;
        case prop_shape::panel:
            count += mesh == prop_mesh::quad ? 1U : 0U;
            break;
        case prop_shape::box:
            count += mesh == prop_mesh::quad ? box_faces : 0U;
            break;
        }
    }
    return count;
}
