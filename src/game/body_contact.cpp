#include "game/body_contact.h"

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "physics/body_collision.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
glm::vec2 flat(const glm::vec3& vector) {
    return glm::vec2(vector.x, vector.z);
}

// A moving cart's circles, from its seat's middle, facing `yaw`: from
// body_behind to body_ahead, close enough that its sides run nearly straight.
std::vector<glm::vec2> cart_offsets(const float yaw, const cart_tuning& tuning) {
    const glm::vec2 forward = flat(yaw_direction(yaw));
    const float length = tuning.body_ahead + tuning.body_behind;
    const int gaps = std::max(1, static_cast<int>(std::ceil(length / (0.5f * std::max(0.01f, tuning.body_radius)))));
    std::vector<glm::vec2> offsets;
    for (int i = 0; i <= gaps; ++i) {
        offsets.push_back(forward * (-tuning.body_behind + length * static_cast<float>(i) / static_cast<float>(gaps)));
    }
    return offsets;
}

// The carts in the world (all but mine when `skip_mine`), each a block as
// long as a moving cart's circles reach and as wide.
void add_carts(upright_obstacles& obstacles, const game_state& state, const bool skip_mine) {
    const cart_tuning& tuning = state.tuning.cart;
    const std::vector<cart_in_world> carts = carts_in_world(state);
    // carts_in_world puts mine first.
    for (std::size_t i = skip_mine ? 1 : 0; i < carts.size(); ++i) {
        const glm::vec2 forward = flat(yaw_direction(carts[i].pose.yaw));
        const glm::vec2 middle = flat(carts[i].pose.position) + forward * (0.5f * (tuning.body_ahead - tuning.body_behind));
        obstacles.blocks.push_back(upright_block{middle, forward, 0.5f * (tuning.body_ahead + tuning.body_behind) + tuning.body_radius,
                                                 tuning.body_radius});
    }
}

// Other players on foot, as posts.
void add_people(upright_obstacles& obstacles, const game_state& state) {
    for (const auto& [account, avatar] : state.remote_avatars) {
        if (avatar.mode == motion_mode::walk || avatar.mode == motion_mode::idle) {
            obstacles.posts.push_back(upright_post{flat(avatar.position), state.tuning.player.body_radius});
        }
    }
}
}

void keep_walker_clear(game_state& state, const glm::vec3& before) {
    player_state& player = state.player;
    upright_obstacles carts;
    add_carts(carts, state, false);
    const ground_body body{{glm::vec2(0.0f)}, state.tuning.player.body_radius, 0.0f};
    const body_move moved = move_body({&state.area.obstacles, &carts}, body, before, player.position);
    const bool walking_on = horizontal_distance(player.position, before) > 0.0f;
    if (moved.touched && walking_on && !player.against_obstacle) {
        push_audio_event(state, audio_event_type::player_bump);
    }
    player.against_obstacle = moved.touched && walking_on;
    player.position = moved.position;
}

void keep_cart_clear(game_state& state, const glm::vec3& before) {
    const cart_tuning& tuning = state.tuning.cart;
    cart_state& cart = state.cart;
    upright_obstacles others;
    add_carts(others, state, true);
    add_people(others, state);
    const ground_body body{cart_offsets(cart.pose.yaw, tuning), tuning.body_radius, tuning.scrape_friction};
    const body_move moved =
        move_body({&state.area.obstacles, &state.area.cart_obstacles, &others}, body, before, cart.pose.position);
    if (moved.touched && !cart.against_obstacle) {
        if (std::abs(cart.velocity) >= tuning.crash_sound_speed) {
            push_audio_event(state, audio_event_type::cart_crash);
        }
        cart.velocity = -cart.velocity * tuning.crash_bounce;
    } else if (moved.touched) {
        const float meant = horizontal_distance(cart.pose.position, before);
        const float went = horizontal_distance(moved.position, before);
        cart.velocity *= meant > 0.0f ? std::clamp(went / meant, 0.0f, 1.0f) : 1.0f;
    }
    cart.against_obstacle = moved.touched;
    cart.pose.position = moved.position;
}
