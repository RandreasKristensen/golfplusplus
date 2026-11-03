#pragma once

#include "physics/material_zone.h"
#include "physics/tree_body.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// One hole as authored in assets/world/courses/*/holes/*.json, in the hole's own coordinates.
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
    glm::vec3 tee_position{0.0f};
    glm::vec3 pin_position{0.0f};
    hole_spline spline;
    std::vector<material_zone> material_zones;
    std::vector<tree_instance> trees;
    // A tee box (game/tee_box.h) and its sign at the tee; without one the
    // ball is dropped on the ground there and plays from its lie (a
    // practice station by a green).
    bool tee_box = true;
    // Whether the ribbon's middle may be fairway (hole.json "ribbon_fairway",
    // default true): false keeps the ribbon rough, the zones deciding the rest.
    bool ribbon_fairway = true;
};

// Whether the ribbon's middle `spline.width` is fairway: not when the hole's
// fairway is its fairway outlines, nor when the hole says its ribbon has none.
inline bool ribbon_paints_fairway(const hole_data& hole) {
    return hole.ribbon_fairway &&
        std::none_of(hole.material_zones.begin(), hole.material_zones.end(),
                     [](const material_zone& zone) { return zone.type == material_zone_type::fairway; });
}
