#include "game/knockout.h"

#include "game/game_state.h"
#include "game/mode_dispatch.h"

#include <utility>

std::vector<fallen_body> bodies_in_world(const game_state& state) {
    std::vector<fallen_body> bodies;
    if (is_online(state)) {
        for (const auto& [id, body] : state.online.bodies) {
            bodies.push_back(body);
        }
        return bodies;
    }
    for (fallen_body& body : bodies_lying(state.save.bodies, state.local_unix_ms)) {
        if (body.place_id == state.place.id) {
            bodies.push_back(std::move(body));
        }
    }
    return bodies;
}

void knock_out_offline(game_state& state, const std::int64_t now_ms) {
    fallen_body fallen;
    fallen.place_id = state.place.id;
    fallen.position = state.player.position;
    fallen.position.y = terrain_height(state.area, fallen.position);
    fallen.yaw = state.player.yaw;
    fallen.bag_on_back = state.bag.where == bag_where::on_back;
    fallen.colour = state.favourite_colour;
    fallen.hand = state.hand;
    state.save.bodies = bodies_lying(std::move(state.save.bodies), now_ms);
    state.save.bodies.push_back(body_fallen_at(std::move(fallen), now_ms, state.tuning.knockout));
    state.save_requested = true;
    state.knocked_out = true;
}
