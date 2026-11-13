#include "game/interactables.h"

#include "game/golf_bag.h"
#include "game/social_dap.h"
#include "game/hub_flow.h"
#include "game/hub_scene.h"
#include "game/mode_dispatch.h"
#include "game/progress_rules.h"
#include "physics/vector_math.h"

#include <algorithm>

namespace {
bool claim_pending(const game_state& state, const std::string& id) {
    const std::vector<std::string>& pending = state.online.pending_claims;
    return std::find(pending.begin(), pending.end(), id) != pending.end();
}
}

std::vector<interactable> interactables_in_reach(const game_state& state) {
    std::vector<interactable> found;
    const glm::vec3& me = state.player.position;
    if (!state.hub_flow.travel_course.empty() || state.hub_flow.ball_request) { return found; }
    if (dap_holds_movement(state)) { return found; }
    for (const auto& [account, player] : state.online.players) {
        if (dap_available(state, account)) {
            found.push_back(interactable{interactable_kind::dap, static_cast<std::size_t>(account),
                horizontal_distance(me, player.motion->position), {}});
        }
    }
    if (on_foot(state)) {
        const auto add = [&](interactable_kind kind, const hub_anchor& anchor, std::size_t index = 0) {
            const float distance = horizontal_distance(me, anchor.position);
            if (distance <= anchor.reach) { found.push_back(interactable{kind, index, distance, {}}); }
        };
        if (state.world.hub) {
            add(interactable_kind::tee_book, state.world.hub->book);
        }
        if (state.area.bus) { add(interactable_kind::bus, *state.area.bus); }
        if (course_warmup(state)) {
            for (std::size_t i = 0; i < state.area.cups.size(); ++i) {
                add(interactable_kind::warmup, hub_anchor{warmup_position(state.area, i, state.tuning),
                    state.tuning.server.warmup_reach_meters / state.tuning.scale.meters_per_world_unit}, i);
            }
            if (!state.world.hole_starts.empty()) {
                const auto& start = state.world.hole_starts.front();
                add(interactable_kind::booked_start, hub_anchor{start.position, start.interaction_radius});
            }
        }
    }
    if (state.ball && !shot_playing(state)) {
        const float distance = horizontal_distance(me, state.ball->position);
        if (distance <= state.tuning.player.ball_interact_radius) {
            const bool in_cup = state.ball->place == ball_place::in_cup;
            found.push_back(interactable{state.ball->kind == ball_kind::practice ? interactable_kind::practice_ball :
                in_cup ? interactable_kind::cup_ball : interactable_kind::ball, 0, distance, {}});
        }
    }
    const std::vector<course_world_collectible>& collectibles = state.world.collectibles;
    for (std::size_t i = 0; i < collectibles.size(); ++i) {
        const float distance = horizontal_distance(me, collectibles[i].position);
        if (distance <= collectibles[i].interaction_radius &&
            collectible_available(active_progress(state), collectibles[i], state.skills) && !claim_pending(state, collectibles[i].id)) {
            found.push_back(interactable{interactable_kind::collectible, i, distance, {}});
        }
    }
    const std::vector<course_world_hole_start>& starts = state.world.hole_starts;
    for (std::size_t i = 0; i < starts.size() && i < state.holes.size(); ++i) {
        const float distance = horizontal_distance(me, starts[i].position);
        const bool my_balls_hole = state.ball && state.ball->hole == i;
        // Not while my ball still moves: it is played only once it rests.
        const bool open = !shot_playing(state) &&
            (state.round && !hole_played(*state.round, i) && !my_balls_hole);
        if (distance <= starts[i].interaction_radius && open && !course_warmup(state) && !first_tee_required(state) &&
            !(is_online(state) && state.online.reservation && state.online.reservation->completed)) {
            found.push_back(interactable{interactable_kind::tee, i, distance, {}});
        }
    }
    if (state.hub && !shot_playing(state)) {
        for (std::size_t i = 0; i < state.hub->tee_mats.size(); ++i) {
            const auto& mat = state.hub->tee_mats[i];
            const float distance = horizontal_distance(me, mat.position);
            if (distance <= mat.reach) { found.push_back({interactable_kind::tee, i, distance, {}}); }
        }
    }
    if (const std::optional<float> bag = bag_in_reach(state)) {
        found.push_back(interactable{interactable_kind::bag, 0, *bag, {}});
    }
    if (const std::optional<float> bag = bag_on_fallen_cart_in_reach(state)) {
        found.push_back(interactable{interactable_kind::fallen_bag, 0, *bag, {}});
    }
    if (const std::optional<reachable_tipped_cart> tipped = nearest_tipped_cart(state)) {
        found.push_back(interactable{interactable_kind::tipped_cart, 0, tipped->distance, *tipped});
    }
    return found;
}

std::optional<interactable> nearest_interactable(const game_state& state) {
    const std::vector<interactable> found = interactables_in_reach(state);
    const auto nearer = [](const interactable& a, const interactable& b) {
        return a.distance != b.distance ? a.distance < b.distance : a.kind < b.kind;
    };
    const auto best = std::min_element(found.begin(), found.end(), nearer);
    return best != found.end() ? std::optional<interactable>(*best) : std::nullopt;
}
