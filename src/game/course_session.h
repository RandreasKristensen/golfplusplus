#pragma once

// Starting courses and moving my ball through its life on them: teed on a
// hole, played, holed, picked out of its cup, or given up. The course is one
// place: nothing here moves the player, ends a ride, an item in use or its
// effects, or rebuilds the area; only my ball, its strokes and the shot change.

#include "game/course_definition.h"
#include "game/game_state.h"

#include <cstddef>

#include <glm/vec3.hpp>

// Loads every hole of `course` and its world, standing me at hole 1's start
// with my ball in my pocket and my cart parked beside it (park_cart). Clears
// the round. False, with `state` unchanged, when a hole or the world cannot be
// loaded.
bool start_course(game_state& state, const course_definition& course);

// Tees my ball up on hole `hole_index`, me behind it facing the pin. False
// when the index is out of range or the hole was already played this round,
// and, with a notice, while my ball is in a cup (state.ball in_cup), while it
// is in play on another hole unless `give_up` (which gives that hole up, with
// no score), or, online, while another player's ball waits on that tee
// (tee_in_use).
bool tee_up(game_state& state, std::size_t hole_index, bool give_up);

// Records my ball's hole's strokes; the ball stays in the cup until it is
// picked up. After the last hole the round is finished.
void complete_current_hole(game_state& state);

// Gives up the hole my ball is in play on, with no score: the ball back in my
// pocket, me where I stand. Online the server gives it up too.
void give_up_hole(game_state& state);

// After a finished round: a new round where I stand. Online the server starts
// it there by itself.
void start_next_round(game_state& state);

// My ball is in its cup within player.ball_interact_radius (XZ) of me.
bool cup_ball_in_reach(const game_state& state);

// Picks my ball out of its cup when in reach: then the next hole can start.
// Online the server checks it too (pick_up_ball). False when out of reach.
bool pick_up_cup_ball(game_state& state);
