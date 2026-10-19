#include "physics/material_zone.h"

#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/geometric.hpp>

namespace {
// Radii below this are no zone at all.
constexpr float min_zone_radius = 0.00001f;
}

std::optional<float> zone_normalized_distance(const material_zone& zone, const glm::vec3& position) {
    const glm::vec2 radii = zone.radii;
    if (radii.x < min_zone_radius || radii.y < min_zone_radius) {
        return std::nullopt;
    }
    const glm::vec3 offset = horizontal(position - zone.center);
    const float squared = glm::dot(offset, offset);

    // A circle needs no turn, and skipping it keeps circles exact.
    if (radii.x == radii.y) {
        if (squared > radii.x * radii.x) {
            return std::nullopt;
        }
        return clamp01(std::sqrt(squared) / radii.x);
    }
    // Outside the circle around the ellipse, whatever its rotation. Most
    // queries stop here, before the rotation's sine and cosine.
    const float outer = std::max(radii.x, radii.y);
    if (squared > outer * outer) {
        return std::nullopt;
    }
    // Stretched to a circle of radius radii.x in the zone's own frame.
    const glm::vec3 local = rotate_about_y(offset, -zone.rotation);
    const float x = local.x;
    const float z = local.z * (radii.x / radii.y);
    const float stretched = x * x + z * z;
    if (stretched > radii.x * radii.x) {
        return std::nullopt;
    }
    return clamp01(std::sqrt(stretched) / radii.x);
}

float zone_distance_outside(const material_zone& zone, const glm::vec3& position) {
    const glm::vec2 radii = zone.radii;
    if (radii.x < min_zone_radius || radii.y < min_zone_radius) {
        return std::numeric_limits<float>::max();
    }
    const glm::vec3 local = rotate_about_y(horizontal(position - zone.center), -zone.rotation);
    const float stretched = std::sqrt(local.x * local.x + local.z * (radii.x / radii.y) * local.z * (radii.x / radii.y));
    if (stretched <= radii.x) {
        return 0.0f;
    }
    // The edge is radii.x / stretched of the way out along the line.
    return glm::length(local) * (1.0f - radii.x / stretched);
}

glm::vec3 zone_world_point(const material_zone& zone, const glm::vec2& local) {
    return zone.center + rotate_about_y(glm::vec3(local.x, 0.0f, local.y), zone.rotation);
}

glm::vec2 zone_half_extent(const material_zone& zone) {
    const float c = std::abs(std::cos(zone.rotation));
    const float s = std::abs(std::sin(zone.rotation));
    const glm::vec2 r = zone.radii;
    return glm::vec2(std::sqrt(r.x * c * r.x * c + r.y * s * r.y * s),
                     std::sqrt(r.x * s * r.x * s + r.y * c * r.y * c));
}
