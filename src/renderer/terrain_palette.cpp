#include "renderer/terrain_palette.h"

#include <glm/common.hpp>

glm::vec3 terrain_material_color(const ground_colors& colors, const terrain_material material, const float edge_amount) {
    switch (material) {
    case terrain_material::fairway:
        return glm::mix(colors.fairway, colors.fairway_edge, edge_amount);
    case terrain_material::rough:
        return colors.rough;
    case terrain_material::green:
        return colors.green;
    case terrain_material::bunker:
        return colors.bunker;
    case terrain_material::water:
        return colors.water;
    }
    return colors.rough;
}
