#pragma once

// The tee box at a hole's tee (every hole's but a practice station's by a
// green, hole_data::tee_box): a flat platform of tiles under a turf mat,
// long down the hole. Placed at runtime from the hole (no authored data).
// The ground under it is levelled into a flat pad flush with its top
// (physics/ground_pad.h, levelled by build_ground), so the ball is teed up,
// played and stood on the ground there, the same on the server. Pure.
// Drawn by renderer/world_marker_batch.h.

#include "game/game_tuning.h"
#include "game/hole_data.h"
#include "physics/ground_pad.h"

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct tee_box {
    std::size_t hole = 0;    // index into play_area::holes of the hole it is for
    glm::vec3 center{0.0f};  // the middle of its top: the levelled ground's height
    glm::vec3 down_hole{0.0f, 0.0f, 1.0f};  // horizontal unit vector along its length
    float half_width = 0.0f;   // across the hole
    float half_length = 0.0f;  // down the hole
};

// The box for `hole` (already placed in course coordinates), centred on its
// tee and facing `down_hole`; its height is its pad's level, known once the
// ground is built.
tee_box place_tee_box(const hole_data& hole, const glm::vec3& down_hole, const tee_box_tuning& tuning);

// The pad the ground is levelled into under `box`.
ground_pad tee_box_pad(const tee_box& box);

// True when `position` (horizontally) is on the box's top.
bool on_tee_box(const tee_box& box, const glm::vec3& position);
// How far `position` is (horizontally) from the box's edge; 0 on it.
float distance_from_tee_box(const tee_box& box, const glm::vec3& position);

// Hole `hole`'s box among `boxes`, or null when it has none.
const tee_box* hole_tee_box(const std::vector<tee_box>& boxes, std::size_t hole);
