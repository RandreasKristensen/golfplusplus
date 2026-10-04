#pragma once

// Moving between courses, holes and the hub. Each call leaves the state ready
// to update: area built, anchors refreshed, transient play state reset.

#include "game/course_definition.h"
#include "game/game_state.h"

#include <cstddef>

#include <glm/vec3.hpp>

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

// Gives up the hole being played on a hub course, with no score: back in
// the hub at `position` (online: where the server has the player).
void abandon_hole(game_state& state, const glm::vec3& position);

// After a finished round on a hub course: a new round, in the hub at the
// last hole's return point. Online the server starts it there by itself.
void start_next_round(game_state& state);

// Puts the ball back on the tee (no penalty stroke). No-op in the hub.
void retee_ball(game_state& state);
