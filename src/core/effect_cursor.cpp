#include "core/effect_cursor.h"

#include "core/play_screen.h"
#include "game/golf_cart.h"

#include <algorithm>

namespace {
// The frame the capture returns in and the one after, which carries the free
// cursor's counts from between two polls.
constexpr int frames_to_settle = 2;
}

bool wants_effect_cursor(const game_state& game, const bool right_held) {
    return right_held && game.mode == game_mode::walking && on_foot(game) &&
        play_frame_for(game, false, false).screen == play_screen::round;
}

effect_cursor_state next_effect_cursor(const effect_cursor_state& previous, const bool wants) {
    if (wants) {
        return effect_cursor_state{true, 0};
    }
    return effect_cursor_state{false, previous.freed ? frames_to_settle : std::max(0, previous.settle_frames - 1)};
}

bool look_blocked(const effect_cursor_state& state) {
    return state.freed || state.settle_frames > 0;
}
