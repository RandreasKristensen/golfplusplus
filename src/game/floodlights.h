#pragma once

// The floodlight poles along every hole: alternating sides down the fairway,
// out past its edge, one beside the tee box and one each side of the green,
// each with a lamp head up top aimed down at what it lights. Placed at
// runtime from the holes and the finished ground (no authored data), so every
// imported course has them, never on fairway, green, tee, sand, water or a
// cart road, nor against a tree or another post. Pure: the server builds the
// same poles, and they stop shots like tree trunks (play_area::floodlight_poles).
// Their light after dark is core/light_frame.h's; drawn by
// renderer/floodlight_batch.h.

#include "game/course_world_definition.h"
#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/tree_collision.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

enum class floodlight_target { fairway, tee, green };

struct floodlight {
    std::size_t area_hole = 0;  // index into play_area::holes of the hole it lights
    floodlight_target target = floodlight_target::fairway;
    glm::vec3 foot{0.0f};  // on the ground
    float radius = 0.0f;
    glm::vec3 lamp{0.0f};  // the lamp head's middle, at the top of the pole
    glm::vec3 aim{0.0f, -1.0f, 0.0f};  // unit, from the lamp towards what it lights
};

// Every hole's poles (`holes` already placed in `area`'s coordinates, its tee
// boxes, signs and fences already standing), hole by hole: the tee's, the
// fairway's from the tee down, then the green's.
std::vector<floodlight> place_floodlights(const play_area& area,
                                          const std::vector<hole_data>& holes,
                                          const std::vector<course_world_cart_road>& roads,
                                          const game_tuning& tuning);

// The poles as obstacles: trunks without leaves, up to their lamps.
std::vector<tree_body> floodlight_poles(const std::vector<floodlight>& lights);
