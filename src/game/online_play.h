#pragma once

// An online round's side of what the server answers. The client plays ahead
// of the server, and here the server's word wins: a refused action puts the
// player and my ball back where the server has them and shows why (a refused
// pickup puts the ball back in its cup, a refused tee-up gives back the ball I
// had, a refused cart seat stands me up, a refused bag move or club puts them
// back as my row has them). A shot is the exception: my swing waits at its
// top for the shot the server played (its event, my address's answer), and a
// refused one ends the swing over the ball. The greens' flags are the room's
// rows. A hole is over when the server says so (its ball row in the cup), not
// when the cup catches it here. Offline nothing here runs.

#include "game/game_state.h"

void adopt_online_arrival(game_state& state);

// Every frame of the live world, whatever input it takes (game_state.h
// update_game). While the server prepares my ball the room plays on, but my
// ball, motion and player row wait for its answer.
void update_online_play(game_state& state, float dt);
