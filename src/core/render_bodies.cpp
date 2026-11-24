#include "core/render_bodies.h"

#include "game/fallen_body.h"
#include "game/gear_paint.h"
#include "game/knockout.h"
#include "game/loadout.h"
#include "game/mode_dispatch.h"
#include "physics/vector_math.h"

namespace {
// The model of the bag the fallen golfer carries: mine, or what their row
// equips, or the default one before it.
std::string bag_model_of_body(const game_state& game, const fallen_body& body) {
    const bool mine = body.account == 0 || body.account == game.online.account_id;
    const auto theirs = game.online.loadouts.find(body.account);
    const loadout equipped = mine ? equipped_loadout(game) : theirs != game.online.loadouts.end() ? theirs->second : game.default_loadout;
    const item_definition* item = game.items.find(fitted_loadout(equipped, game.default_loadout, game.items).bag);
    return item != nullptr ? item->model : std::string();
}
}

void add_lying_bodies(render_data& data, const game_state& game) {
    const float unit = game.tuning.scale.meters_per_world_unit;
    for (const fallen_body& body : bodies_in_world(game)) {
        render_lying_body lying;
        lying.position = body.position;
        lying.yaw = body.yaw;
        lying.account = shirt_id_of(game, body.account);
        lying.length = game.tuning.rangefinder.person_height / unit;
        lying.half_width = game.tuning.knockout.body_half_width_meters / unit;
        lying.height = game.tuning.knockout.body_height_meters / unit;
        const glm::vec3 along = yaw_direction(body.yaw) * (0.5f * lying.length);
        const glm::vec3 stations[3] = {body.position - along, body.position, body.position + along};
        for (std::size_t i = 0; i < 3; ++i) {
            lying.ground[i] = terrain_height(game.area, stations[i]);
        }
        if (body.bag_on_back) {
            lying.bag_model = bag_model_of_body(game, body);
            lying.bag_paint = gear_paint{body.colour, body.account};
        }
        data.lying_bodies.push_back(std::move(lying));
    }
}
