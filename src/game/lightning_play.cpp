#include "game/lightning_play.h"

#include "game/ball_hit_rules.h"
#include "game/game_state.h"
#include "game/knockout.h"
#include "game/lightning_rules.h"
#include "game/mode_dispatch.h"
#include "game/shot_dice.h"

#include <utility>

namespace {
// What a strike does to me offline: its bolt and a daze or a knockout.
void strike_me_offline(game_state& state, const lightning_strike_hit& hit) {
    const std::int64_t now_ms = shot_unix_ms(state);
    state.strike_points.push_back(hit.position);
    if (hit.outcome == lightning_outcome::knocked_out) {
        knock_out_offline(state, state.local_unix_ms);
    } else if (const event_effect_definition* dazed = state.event_effects.find(dazed_effect_id)) {
        state.effects = effects_after_event(running_effects(std::move(state.effects), now_ms), *dazed, now_ms);
    }
}
}

void strike_me(game_state& state, const bool knock_out) {
    if (is_online(state)) {
        record_god_strike(state, knock_out);
        return;
    }
    strike_me_offline(state, lightning_strike_hit{0, state.player.position,
                                                  knock_out ? lightning_outcome::knocked_out : lightning_outcome::dazed});
}

void update_offline_lightning(game_state& state) {
    if (is_online(state)) {
        return;
    }
    const std::int64_t now_ms = shot_unix_ms(state);
    const std::int64_t check = lightning_check_at(now_ms, state.tuning.lightning);
    if (state.lightning_seed == 0) {
        state.lightning_seed = round_shot_seed(static_cast<std::uint64_t>(state.local_unix_ms), 1);
    }
    if (state.lightning_checked == 0) {
        state.lightning_checked = check;  // the check under way is not rolled
    }
    if (check <= state.lightning_checked) {
        return;
    }
    state.lightning_checked = check;
    const lightning_golfer me{0, state.player.position, state.mode == game_mode::addressing};
    const std::vector<lightning_strike_hit> hits =
        lightning_strikes({me}, state.weather.thunder, lightning_check_seed(state.lightning_seed, check), state.area.solids,
                          state.area.solids_grid, state.tuning);
    for (const lightning_strike_hit& hit : hits) {
        strike_me_offline(state, hit);
    }
}
