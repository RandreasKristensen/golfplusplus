#pragma once

// The ink a surface is printed in on any map of the course (the course map
// and the hole signs' faces): the map's own palette (map_inks), so every map
// agrees with the others. GL-free.

#include "game/lighting_loader.h"
#include "physics/terrain.h"

#include <glm/vec3.hpp>

// The ink of a surface at full strength.
glm::vec3 map_material_ink(const map_inks& inks, terrain_material material);

// The ink of a surface on a hole sign's map: another hole's fairway, rough
// and green fade towards the paper (map_inks::other_hole_fade) so the hole
// shown stands out; bunkers and water are as they are.
glm::vec3 map_hole_ink(const map_inks& inks, terrain_material material, bool own_hole);
