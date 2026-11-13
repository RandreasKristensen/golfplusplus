#include "core/tape_tracking.h"

#include <algorithm>

tape_tracking_signals tape_tracking_signals_for(const game_state& game, const bool paused) {
    return tape_tracking_signals{paused, ball_in_play(game), game.mode != game_mode::walking,
                                 !game.hub_flow.travel_course.empty()};
}

tape_tracking_state update_tape_tracking(const tape_tracking_state& state, const tape_tracking_signals& signals,
                                         const float dt, const tape_style& tape) {
    tape_tracking_state next = state;
    const bool arrived = state.travelling && !signals.travelling;
    next.burst_left = signals.on_hole != state.on_hole || arrived ? tape.tracking_burst_seconds
                                                                  : std::max(0.0f, state.burst_left - dt);
    next.on_hole = signals.on_hole;
    next.travelling = signals.travelling;
    const float fade_step = dt / tape.travel_fade_seconds;
    next.fade = signals.travelling ? std::min(1.0f, state.fade + fade_step) : std::max(0.0f, state.fade - fade_step);
    if (signals.in_play) {
        next.level = 0.0f;
        return next;
    }
    const float target = signals.paused || next.burst_left > 0.0f ? 1.0f : 0.0f;
    const float step = dt / tape.tracking_fade_seconds;
    next.level = target > state.level ? std::min(target, state.level + step) : std::max(target, state.level - step);
    return next;
}
