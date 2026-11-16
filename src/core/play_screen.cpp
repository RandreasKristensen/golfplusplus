#include "core/play_screen.h"

#include "game/group_round.h"
#include "game/mode_dispatch.h"

play_frame play_frame_for(const game_state& game, const bool confirm_active, const bool escape_pressed) {
    if (game.hub_flow.screen != hub_screen::none || !game.hub_flow.travel_course.empty()) {
        return play_frame{play_screen::hub_menu, true};
    }
    if (round_results_shown(game)) {
        return play_frame{play_screen::results, is_online(game)};
    }
    const bool opens_pause = escape_pressed && game.mode == game_mode::walking && !game.give_up_prompt && !game.picker &&
        !game.pacing && !game.hub_flow.ball_request;
    if (confirm_active || opens_pause) {
        return play_frame{play_screen::pause, is_online(game)};
    }
    return play_frame{play_screen::round, true};
}
