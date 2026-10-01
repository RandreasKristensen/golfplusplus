#pragma once

#include "physics/material_zone.h"
#include "physics/tree_collision.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// One hole as authored in assets/holes/*.json, in the hole's own coordinates.
struct hole_spline {
    std::vector<glm::vec3> control_points;
    std::vector<float> bank;  // per control point, see terrain_spline::bank; empty = level
    float width = 0.0f;        // fairway
    float rough_width = 0.0f;  // fairway plus rough, >= width
};

struct tree_instance {
    glm::vec3 position{0.0f};  // authored position; the height comes from the terrain
    tree_shape shape;
};

struct hole_data {
    std::string id;
    std::string name;
    int par = 0;
    std::uint32_t wind_seed = 0;
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    hole_spline spline;
    std::vector<material_zone> material_zones;
    std::vector<tree_instance> trees;
};
