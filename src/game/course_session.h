#pragma once

// Moving between courses, holes and the hub. Each call leaves the state ready
// to update: area built, anchors refreshed, transient play state reset.

#include "game/course_definition.h"
#include "game/game_state.h"

#include <cstddef>

#include <glm/vec3.hpp>

// Loads every hole of `course` and its world, then enters the hub at hole 1's
// start. Clears the round. False, with `state` unchanged, when a hole or the
// world cannot be loaded.
bool start_course(game_state& state, const course_definition& course);

// Plays hub hole `hole_index` where it sits on the course: the area stays the
// whole course, with every hole, tree and the land. False when there is no
// hub, the index is out of range or the hole was already played this round,
// and, with a notice, while the last holed ball is still in its cup
// (state.cup_ball) or, online, another player is using its tee (tee_in_use).
bool start_hub_hole(game_state& state, std::size_t hole_index);

// Records the hole's strokes, then returns to the hub where the player
// stands, the ball left in the cup (state.cup_ball). After the last hole the
// round is finished and the state stays put.
void complete_current_hole(game_state& state);

// Gives up the hole being played, with no score: back in the hub at
// `position` (online: where the server has the player).
void abandon_hole(game_state& state, const glm::vec3& position);

// After a finished round: a new round, in the hub where the player stands.
// Online the server starts it there by itself.
void start_next_round(game_state& state);

// Puts the ball back on the tee (no penalty stroke). No-op in the hub.
void retee_ball(game_state& state);

// The ball left in its cup is within player.ball_interact_radius (XZ) of the
// player walking the hub.
bool cup_ball_in_reach(const game_state& state);

// Picks the ball out of its cup when in reach: then the next hole can start.
// Online the server checks it too (pick_up_ball). False when out of reach.
bool pick_up_cup_ball(game_state& state);
