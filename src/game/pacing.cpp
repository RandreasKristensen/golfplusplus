#include "game/pacing.h"

#include "game/game_state.h"
#include "game/golf_cart.h"

#include <algorithm>
#include <cmath>

int paces_in(const float meters, const float pace_meters) {
    return pace_meters > 0.0f ? static_cast<int>(std::floor(std::max(0.0f, meters) / pace_meters)) : 0;
}

void update_pacing(game_state& state, const game_input& input) {
    const bool can_pace = state.mode == game_mode::walking && on_foot(state) && state.player.vertical.grounded;
    const bool moving = walk_direction(input) != 0.0f || strafe_direction(input) != 0.0f;
    const bool broken = input.jump || input.cancel || (input.run_held && moving);
    if (state.pacing && (!can_pace || broken || input.toggle_pacing)) {
        state.pacing.reset();
        return;
    }
    if (!state.pacing) {
        if (input.toggle_pacing && can_pace && !broken) {
            state.pacing = pacing_walk{};
            state.pace_count = pace_count{};
        }
        return;
    }
}

void count_paces(game_state& state, const float meters) {
    if (!state.player.vertical.grounded) {
        state.pacing.reset();
    }
    if (!state.pacing) {
        return;
    }
    const float pace_meters = state.tuning.pacing.pace_meters;
    const int paces = paces_in(state.pacing->meters, pace_meters);
    state.pacing->meters += meters;
    const int now = paces_in(state.pacing->meters, pace_meters);
    if (now != paces) {
        state.pace_count = pace_count{now, 0.0f};
    }
}

void update_pace_count(game_state& state, const float dt) {
    if (!state.pace_count) {
        return;
    }
    if (state.pacing && (state.mode != game_mode::walking || !on_foot(state) || !state.player.vertical.grounded)) {
        state.pacing.reset();
    }
    const shot_number_tuning& shown = state.tuning.shot_numbers;
    state.pace_count->age += std::max(0.0f, dt);
    if (state.pacing) {
        // Held up, short of its fade, while I keep counting.
        state.pace_count->age = std::min(state.pace_count->age, shown.lifetime_seconds * (1.0f - shown.fade_share));
    } else if (state.pace_count->age >= shown.lifetime_seconds) {
        state.pace_count.reset();
    }
}
