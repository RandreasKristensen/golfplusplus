#pragma once
// Online F greeting intents and current paired presentation. Offline has no
// social partner or progress path. Pending commands never predict an animation.
#include "game/game_state.h"
const room_dap* active_dap(const game_state& state, std::uint64_t account);
bool dap_available(const game_state& state, std::uint64_t target);
void request_dap(game_state& state, std::uint64_t target);
void reconcile_dap(game_state& state);
bool dap_holds_movement(const game_state& state);
