#include "core/progress_saver.h"

#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/save_manager.h"

bool save_progress(game_state& state, const progress_saver& saver) {
    state.save_requested = false;
    return state.play != play_mode::offline || !saver.enabled || write_save(saver.path, state.save);
}

bool enter_place_saving(game_state& state, const std::string& place_id, const progress_saver& saver) {
    const bool bus_from_round = state.kind == place_kind::course && state.hub_flow.travel_course == online_hub_course;
    if (!enter_place(state, place_id)) {
        return false;
    }
    if (bus_from_round && state.kind == place_kind::hub) {
        save_progress(state, saver);
    }
    return true;
}
