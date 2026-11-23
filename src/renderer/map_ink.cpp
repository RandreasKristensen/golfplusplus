#include "renderer/map_ink.h"

#include <glm/common.hpp>

glm::vec3 map_material_ink(const map_inks& inks, const terrain_material material) {
    switch (material) {
    case terrain_material::fairway:
        return inks.fairway;
    case terrain_material::rough:
        return inks.rough;
    case terrain_material::heavy_rough:
        return inks.heavy_rough;
    case terrain_material::green:
        return inks.green;
    case terrain_material::bunker:
        return inks.bunker;
    case terrain_material::water:
        return inks.water;
    }
    return inks.rough;
}

glm::vec3 map_hole_ink(const map_inks& inks, const terrain_material material, const bool own_hole) {
    const glm::vec3 ink = map_material_ink(inks, material);
    const bool hazard = material == terrain_material::bunker || material == terrain_material::water;
    return own_hole || hazard ? ink : glm::mix(ink, inks.paper, inks.other_hole_fade);
}
