#pragma once

// The tee box at every hole's tee: a flat platform of tiles under a turf mat,
// long down the hole. Placed at runtime from the hole and the ground under it
// (no authored data). Its top sits just over the highest ground it covers, so
// it never dips into the land; sample_area returns that top inside it, so the
// ball is teed up, played and stood on there, the same on the server. Pure.
// Drawn by renderer/world_marker_batch.h.

#include "game/game_tuning.h"
#include "game/hole_data.h"

#include <vector>

#include <glm/vec3.hpp>

struct play_area;

struct tee_box {
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
