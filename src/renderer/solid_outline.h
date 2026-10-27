#pragma once

// What is solid, as the god menu shows it for checking an import: every
// edge of a solid (physics/solid.h) as a segment in the world, drawn as OSD
// lines (osd_overlay.h). GL-free.

#include "physics/solid.h"

#include <utility>
#include <vector>

#include <glm/vec3.hpp>

using world_segment = std::pair<glm::vec3, glm::vec3>;

// A cylinder's or cone's foot and top rings (a cone's tip) with uprights
// between them, a wall's and a box's edges.
std::vector<world_segment> solid_outline(const solid& thing);
