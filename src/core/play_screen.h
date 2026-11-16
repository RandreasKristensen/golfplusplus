#pragma once

// Which screen a frame of play goes to in app, and whether the world updates
// under it. Online the world never waits for a screen: under the pause menu
// and the results it plays on with neutral input (update_game in
// game/game_state.h decides what input may begin). Offline the pause menu and
// the results hold it still. The hub's screens and travel always update it;
// update_game itself freezes what must wait there. GL-free.

#include "game/game_state.h"

enum class play_screen {
    hub_menu,  // the tee book, the bus menu or travel
    results,   // my finished round's results
    pause,     // the pause menu, open or opening this frame
    round      // playing, with the player's input
};

struct play_frame {
    play_screen screen = play_screen::round;
    bool world_updates = true;
};

play_frame play_frame_for(const game_state& game, bool confirm_active, bool escape_pressed);
