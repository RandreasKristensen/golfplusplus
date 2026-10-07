#pragma once

// An online round's side of what the server answers. The client plays ahead
// of the server (a shot plays as soon as it is hit), and here the server's
// word wins: a refused action puts the player and ball back where the server
// has them and shows why (a refused pickup puts the ball back in its cup); my shot's event moves my ball to where the server
// says it rests; and a hole is over when the server says so (my ball holed
// and me back in the hub), not when the cup catches it here. Offline nothing
// here runs.

#include "game/game_state.h"

void update_online_play(game_state& state, float dt);
// While my finished round waits for the group (game/group_round.h): others'
// shots and emotes play, notices age, and a refused move of mine puts me back.
void watch_room(game_state& state, float dt);
