#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

struct hole_spline {
    std::vector<glm::vec3> control_points;
    float width = 0.0f;
};

struct hole_data {
    std::string id;
    std::string name;
    int par = 3;
    std::uint32_t wind_seed = 42U;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f, 0.0f, 80.0f};
    hole_spline spline;
    std::vector<material_zone> material_zones;
};
