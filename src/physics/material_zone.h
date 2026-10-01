#pragma once

// Authored surface zones (green, bunker, water) that shape and colour the terrain.

#include <glm/vec3.hpp>

enum class material_zone_type {
    green,
    bunker,
    water,
    unknown  // unrecognised type in the data; ignored by terrain building
};

// A circle (`has_radius`) or an axis-aligned box (`has_bounds`) on the ground.
// If both are set, the circle is used.
struct material_zone {
    material_zone_type type = material_zone_type::unknown;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
    bool has_radius = false;
    bool has_bounds = false;
};
