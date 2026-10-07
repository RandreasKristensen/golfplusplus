#pragma once

// Small pure vector helpers shared by physics, game and renderer. Use these
// (and glm::radians / glm::pi) instead of local copies.

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
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

// `point` turned `radians` around the Y axis through the origin, +X towards +Z.
// Placing a hole on its course turns it this way.
inline glm::vec3 rotate_about_y(const glm::vec3& point, const float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return glm::vec3(point.x * c - point.z * s, point.y, point.x * s + point.z * c);
}

// Yaw convention for players, carts and aiming: 0 faces +Z, positive turns
// towards +X.
inline glm::vec3 yaw_direction(const float yaw) {
    return glm::vec3(std::sin(yaw), 0.0f, std::cos(yaw));
}

// `angle` in [-pi, pi). A shot's aim is wrapped before it is simulated or
// sent, so every machine takes the same sine and cosine of it.
inline float wrap_angle(const float angle) {
    const float pi = glm::pi<float>();
    const float two_pi = glm::two_pi<float>();
    float wrapped = angle - two_pi * std::floor((angle + pi) / two_pi);
    // Rounding can leave a large angle just outside the turn.
    if (wrapped >= pi) {
        wrapped -= two_pi;
    }
    if (wrapped < -pi) {
        wrapped += two_pi;
    }
    return wrapped;
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
