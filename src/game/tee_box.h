#pragma once

// The tee box at a hole's tee (every hole's but a practice station's by a
// green, hole_data::tee_box): a flat platform of tiles under a turf mat,
// long down the hole. Placed at runtime from the hole and the ground under it
// (no authored data). Its top sits tee_box.padding over the highest ground it
// covers, so it never dips into the land. As a solid (game/area_solids.h) its
// top is walkable, the surface there, so the ball is teed up, played and
// stood on it, the same on the server, and its sides stand in the way. Pure.
// Drawn by renderer/world_marker_batch.h.

#include "game/game_tuning.h"
#include "game/hole_data.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct tee_box {
    std::size_t hole = 0;    // index into play_area::holes of the hole it is for
    glm::vec3 center{0.0f};  // the middle of its top
    glm::vec3 down_hole{0.0f, 0.0f, 1.0f};  // horizontal unit vector along its length
    float half_width = 0.0f;   // across the hole
    float half_length = 0.0f;  // down the hole
    float bottom = 0.0f;       // the lowest ground under it: its sides reach this far down
};

// The box for `hole` (already placed in `area`'s coordinates), centred on its
// tee and facing `down_hole`, over `area`'s ground.
tee_box place_tee_box(const play_area& area, const hole_data& hole, const glm::vec3& down_hole, const tee_box_tuning& tuning);

// True when `position` (horizontally) is on the box's top.
bool on_tee_box(const tee_box& box, const glm::vec3& position);

// Hole `hole`'s box among `boxes`, or null when it has none.
const tee_box* hole_tee_box(const std::vector<tee_box>& boxes, std::size_t hole);
