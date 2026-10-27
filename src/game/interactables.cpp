#include "game/interactables.h"

#include "game/golf_bag.h"
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
    if (state.ball && !shot_playing(state)) {
        const float distance = horizontal_distance(me, state.ball->position);
        if (distance <= state.tuning.player.ball_interact_radius) {
            const bool in_cup = state.ball->place == ball_place::in_cup;
            found.push_back(interactable{in_cup ? interactable_kind::cup_ball : interactable_kind::ball, 0, distance, {}, {}});
        }
    }
    const std::vector<course_world_collectible>& collectibles = state.world.collectibles;
    for (std::size_t i = 0; i < collectibles.size(); ++i) {
        const float distance = horizontal_distance(me, collectibles[i].position);
        if (distance <= collectibles[i].interaction_radius &&
            collectible_available(active_progress(state), collectibles[i]) && !claim_pending(state, collectibles[i].id)) {
            found.push_back(interactable{interactable_kind::collectible, i, distance, {}, {}});
        }
    }
    const std::vector<course_world_hole_start>& starts = state.world.hole_starts;
    for (std::size_t i = 0; i < starts.size() && i < state.holes.size(); ++i) {
        const float distance = horizontal_distance(me, starts[i].position);
        const bool my_balls_hole = state.ball && state.ball->hole == i;
        if (distance <= starts[i].interaction_radius && !hole_played(state.round, i) && !my_balls_hole) {
            found.push_back(interactable{interactable_kind::tee, i, distance, {}, {}});
        }
    }
    if (const std::optional<float> bag = bag_in_reach(state)) {
        found.push_back(interactable{interactable_kind::bag, 0, *bag, {}, {}});
    }
    if (const std::optional<reachable_seat> seat = nearest_seat(state)) {
        found.push_back(interactable{interactable_kind::seat, 0, seat->distance, *seat, {}});
    }
    if (const std::optional<reachable_tipped_cart> tipped = nearest_tipped_cart(state)) {
        found.push_back(interactable{interactable_kind::tipped_cart, 0, tipped->distance, {}, *tipped});
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
