#pragma once

// The signs pointing the way from each green but the last to the next
// hole's tee: a board on the front of one post, its pointed end the way to
// go. Where the cart paths (play_area::cart_paths) lead from by the green to
// by the next tee, one stands beside the path where it leaves the green,
// pointing along it, and one just before every junction on the way (where
// there is room), pointing the way to take there. Where they do not, one
// stands just past the green's edge on the way to the tee, pointing straight
// at it. Each keeps the paths' clearance and its face looks at the way it
// points along. Placed at runtime from the holes and the paths; its post and
// board stop shots.

#include "game/game_tuning.h"
#include "game/hole_data.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct way_sign {
    std::size_t to_hole = 0;  // index into play_area::holes of the hole whose tee it points to
    glm::vec3 points{0.0f, 0.0f, 1.0f};       // horizontal unit vector: the way to go
    glm::vec3 face_normal{1.0f, 0.0f, 0.0f};  // horizontal unit vector: towards the way it points along
    glm::vec3 post_foot{0.0f};
    glm::vec3 board_center{0.0f};
    float board_half_width = 0.0f;  // along `points`
    float board_half_height = 0.0f;
    float board_thickness = 0.0f;
    float post_radius = 0.0f;
};

// The signs for `holes` (placed in `area`'s coordinates, its cart paths laid),
// `path_clearance` clear of every path where there is room.
std::vector<way_sign> place_way_signs(const play_area& area, const std::vector<hole_data>& holes,
                                      const way_sign_tuning& tuning, float path_clearance);

float way_sign_top(const way_sign& sign);
