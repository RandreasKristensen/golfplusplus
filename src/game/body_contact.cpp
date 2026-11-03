#include "game/body_contact.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "physics/body_collision.h"
#include "physics/vector_math.h"

#include <vector>

namespace {
// Every cart in the world (all but mine when `skip_mine`), as shown.
std::vector<cart_pose> shown_carts(const game_state& state, const bool skip_mine) {
    std::vector<cart_pose> carts;
    for (const cart_in_world& shown : carts_in_world(state)) {
        if (!skip_mine || shown.owner != 0) {
            carts.push_back(shown.pose);
        }
    }
    return carts;
}

// Other players on foot, where they are shown.
std::vector<glm::vec3> people_on_foot(const game_state& state) {
    std::vector<glm::vec3> people;
    for (const auto& [account, avatar] : state.remote_avatars) {
        if (avatar.mode == motion_mode::walk || avatar.mode == motion_mode::idle) {
            people.push_back(avatar.position);
        }
    }
    return people;
}
}

std::vector<solid> walking_obstacles(const game_state& state) {
    return walker_obstacles(shown_carts(state, false), state.area.cups, state.flags_in, state.tuning);
}

void keep_walker_clear(game_state& state, const glm::vec3& before) {
    player_state& player = state.player;
    const player_tuning& tuning = state.tuning.player;
    const std::vector<solid> moving = walking_obstacles(state);
    const body_move moved = move_body(body_obstacles{state.area.solids, state.area.solids_grid, moving, solid_mover::walker},
                                      circle_body(tuning.body_radius, 0.0f, tuning.step_up), before, player.position);
    player.against_obstacle = moved.touched && horizontal_distance(player.position, before) > 0.0f;
    player.position = moved.position;
}

std::vector<solid> cart_obstacles(const game_state& state) {
    return cart_obstacles_of(shown_carts(state, true), people_on_foot(state), state.area.cups, state.flags_in, state.tuning);
}
