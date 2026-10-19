#pragma once

// Ground colours by surface, from lighting.json's ground colours. Shared by
// the 3D terrain and every 2D view of a hole (menu thumbnails, course map,
// sign faces), so they always agree.

#include "game/lighting_loader.h"
#include "physics/terrain.h"

#include <glm/vec3.hpp>

// `edge_amount` (0 at the fairway centre, 1 at its edge) shades the fairway
// towards its edge colour.
glm::vec3 terrain_material_color(const ground_colors& colors, terrain_material material, float edge_amount = 0.0f);
