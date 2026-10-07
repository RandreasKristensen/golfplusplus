#pragma once

// Authored surface zones (green, bunker, water) that shape and colour the
// terrain, and the one test of where a point sits in one: everything that asks
// whether a point is in a zone, or how far in, goes through here.

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

enum class material_zone_type {
    green,
    bunker,
    water,
    unknown  // unrecognised type in the data; ignored by terrain building
};

// An ellipse on the ground around `center`, with semi-axes `radii.x` along
// its own x and `radii.y` along its own z (a circle has equal radii), turned
// by `rotation` radians about the vertical (rotate_about_y,
// physics/vector_math.h). A placed hole adds its own turn, the way it turns
// everything else in the hole.
struct material_zone {
    material_zone_type type = material_zone_type::unknown;
    glm::vec3 center{0.0f};
    glm::vec2 radii{0.0f};
    float rotation = 0.0f;
};

// How far into the zone `position` is, ignoring height: 0 at the centre, 1 on
// the edge, the fraction of the way out along the line from the centre. For a
// circle that is distance / radius; an ellipse is that circle stretched along
// its axes. Edge blends (a bunker's lip, a pond's bank) fall off along it, so
// they keep their shape however the zone is stretched. nullopt outside.
std::optional<float> zone_normalized_distance(const material_zone& zone, const glm::vec3& position);

inline bool zone_contains(const material_zone& zone, const glm::vec3& position) {
    return zone_normalized_distance(zone, position).has_value();
}

// The world point at `local` (x, z) in the zone's own frame: from its centre,
// before its rotation. Height is the centre's.
glm::vec3 zone_world_point(const material_zone& zone, const glm::vec2& local);

// Half the size of the turned zone along world x and z: the box around it.
glm::vec2 zone_half_extent(const material_zone& zone);
