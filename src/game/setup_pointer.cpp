#include "game/setup_pointer.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// The angle at `eye` between `direction` and the point of segment a-b
// nearest the view's line.
float angle_to_segment(const glm::vec3& eye, const glm::vec3& direction, const glm::vec3& a, const glm::vec3& b) {
    const glm::vec3 along = b - a;
    const glm::vec3 from_eye = a - eye;
    const float d = glm::dot(along, direction);
    const float length2 = glm::dot(along, along);
    // Closest points of the line a + s along and the ray eye + t direction.
    const float denominator = length2 - d * d;
    float s = denominator > 1e-8f ? (d * glm::dot(from_eye, direction) - glm::dot(from_eye, along)) / denominator : 0.0f;
    s = clamp01(s);
    const glm::vec3 to_point = a + along * s - eye;
    const float distance = glm::length(to_point);
    if (distance <= 0.0f) {
        return 0.0f;
    }
    return std::acos(std::clamp(glm::dot(to_point / distance, direction), -1.0f, 1.0f));
}
}

setup_part pointed_part(const glm::vec3& eye, const glm::vec3& direction, const address_marks& marks,
                        const float radius_degrees) {
    const std::array<std::pair<setup_part, float>, 4> parts{{
        {setup_part::face, angle_to_segment(eye, direction, marks.face_heel, marks.face_toe)},
        {setup_part::ball, angle_to_segment(eye, direction, marks.ball, marks.ball)},
        {setup_part::stance, angle_to_segment(eye, direction, marks.stick_back, marks.stick_front)},
        {setup_part::stance, angle_to_segment(eye, direction, marks.stance, marks.stance)},
    }};
    setup_part nearest = setup_part::none;
    float best = glm::radians(radius_degrees);
    for (const auto& [part, angle] : parts) {
        if (angle <= best) {
            best = angle;
            nearest = part;
        }
    }
    return nearest;
}
