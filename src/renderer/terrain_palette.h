#pragma once

// Ground colours by surface. Shared by the 3D terrain and every 2D view of a
// hole (menu thumbnails, course map), so they always agree.

#include "physics/terrain.h"

#include <glm/vec3.hpp>

// `edge_amount` (0 at the fairway centre, 1 at its edge) shades the fairway
// slightly towards the rough.
glm::vec3 terrain_material_color(terrain_material material, float edge_amount = 0.0f);
