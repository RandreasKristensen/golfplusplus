#pragma once

// Ground colours by surface, from lighting.json's ground colours. Shared by
// the 3D terrain and the menu thumbnails of a hole. The printed maps (course
// map, sign faces) have their own inks (renderer/map_ink.h).

#include "game/lighting_loader.h"
#include "physics/terrain.h"

#include <glm/vec3.hpp>

// `edge_amount` (0 at the fairway centre, 1 at its edge) shades the fairway
// towards its edge colour.
glm::vec3 terrain_material_color(const ground_colors& colors, terrain_material material, float edge_amount = 0.0f);
