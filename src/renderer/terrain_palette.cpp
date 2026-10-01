#include "renderer/terrain_palette.h"

glm::vec3 terrain_material_color(const terrain_material material, const float edge_amount) {
    switch (material) {
    case terrain_material::fairway:
        return glm::vec3(0.17f + edge_amount * 0.04f, 0.44f - edge_amount * 0.08f, 0.17f + edge_amount * 0.02f);
    case terrain_material::rough:
        return glm::vec3(0.12f, 0.27f, 0.12f);
    case terrain_material::green:
        return glm::vec3(0.20f, 0.68f, 0.28f);
    case terrain_material::bunker:
        return glm::vec3(0.66f, 0.55f, 0.22f);
    case terrain_material::water:
        return glm::vec3(0.10f, 0.22f, 0.60f);
    }
    return glm::vec3(0.12f, 0.27f, 0.12f);
}
