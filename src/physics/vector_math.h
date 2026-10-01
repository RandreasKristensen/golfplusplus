#pragma once

// Small pure vector helpers shared by physics, game and renderer. Use these
// (and glm::radians / glm::pi) instead of local copies.

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

inline constexpr glm::vec3 world_up{0.0f, 1.0f, 0.0f};

inline float clamp01(const float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline glm::vec3 safe_normalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float length = glm::length(value);
    return length <= 0.00001f ? fallback : value / length;
}

// Terrain normals can come back zero-length on degenerate triangles.
inline glm::vec3 ground_normal(const glm::vec3& normal) {
    return safe_normalize(normal, world_up);
}

inline glm::vec3 horizontal(const glm::vec3& value) {
    return glm::vec3(value.x, 0.0f, value.z);
}

inline float horizontal_distance(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(horizontal(a - b));
}

// Yaw convention for players, carts and aiming: 0 faces +Z, positive turns
// towards +X.
inline glm::vec3 yaw_direction(const float yaw) {
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

inline float yaw_towards(const glm::vec3& from, const glm::vec3& to) {
    const glm::vec3 delta = horizontal(to - from);
    if (glm::length(delta) <= 0.0001f) {
        return 0.0f;
    }
    return std::atan2(delta.x, delta.z);
}

// Unit vector to the left of `forward` on the ground plane.
inline glm::vec3 yaw_left(const glm::vec3& forward) {
    return safe_normalize(glm::cross(world_up, forward), glm::vec3(1.0f, 0.0f, 0.0f));
}
