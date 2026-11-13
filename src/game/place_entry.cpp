#include "game/mode_dispatch.h"
#include "game/content_files.h"
#include "game/course_loader.h"
#include "game/course_session.h"
#include "game/hub_session.h"

bool enter_place(game_state& state, const std::string& place_id) {
    if (is_online(state) && (!state.online.room_ready || !state.online.room || state.online.room->course_id != place_id)) {
        return false;
    }
    const bool arriving_by_bus = !state.hub_flow.travel_course.empty();
    if (place_id == online_hub_course) {
        const auto hub = load_hub(state.asset_root);
        const bool entered = hub && start_hub(state, *hub);
        if (entered) { state.hub_flow = hub_flow_state{}; state.offline_preparing = false; }
        return entered;
    }
    for (const auto& course : load_courses(state.asset_root)) {
        if (course.id == place_id) {
            const bool entered = start_course(state, course);
            if (entered) {
                state.hub_flow = hub_flow_state{};
                state.offline_preparing = arriving_by_bus && !is_online(state);
            }
            return entered;
        }
    }
    return false;
}
