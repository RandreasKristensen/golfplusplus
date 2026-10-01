#pragma once

// Moving between courses, holes and the hub. Each call leaves the state ready
// to update: area built, anchors refreshed, transient play state reset.

#include "game/course_definition.h"
#include "game/game_state.h"

#include <cstddef>

// Loads every hole of `course`, then enters its hub (when it has a course
// world) or hole 1. Clears the round. False, with `state` unchanged, when a
// hole or the world cannot be loaded.
bool start_course(game_state& state, const course_definition& course);

// Plays hub hole `hole_index` where it sits on the course: the area stays the
// whole course, with every hole, tree and the land. False when there is no
// hub, the index is out of range or the hole was already played this round.
bool start_hub_hole(game_state& state, std::size_t hole_index);

// Records the hole's strokes, then returns to the hub or loads the next hole.
// After the last hole the round is finished and the state stays put.
void complete_current_hole(game_state& state);

// Puts the ball back on the tee (no penalty stroke). No-op in the hub.
void retee_ball(game_state& state);
