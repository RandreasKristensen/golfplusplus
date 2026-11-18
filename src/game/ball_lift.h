#pragma once

// F at my ball: a cup ball lifts immediately; otherwise a tap steps up
// to it and a hold (player.ball_pickup_hold_seconds) lifts where rules allow.
#include "game/game_input.h"
#include "game/game_state.h"

// F pressed with my ball nearest: lifts a cup ball or starts the hold.
void begin_ball_hold(game_state& state);
// Ends the hold at release (a tap steps up to the ball) or lifts the ball once
// held long enough. Call every frame on foot.
void update_ball_hold(game_state& state, const game_input& input, float dt);
// Picks up my nearest resting ball in reach; false when none may be lifted.
bool lift_my_ball(game_state& state);
