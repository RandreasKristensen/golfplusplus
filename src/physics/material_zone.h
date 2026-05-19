#pragma once

#include <glm/vec3.hpp>

enum class material_zone_type {
    green,
    bunker,
    water,
    unknown
};

struct material_zone {
    material_zone_type type = material_zone_type::unknown;
    glm::vec3 center{0.0f};
    float radius = 0.0f;
    glm::vec3 bounds_min{0.0f};
    glm::vec3 bounds_max{0.0f};
    bool has_radius = false;
    bool has_bounds = false;
};
