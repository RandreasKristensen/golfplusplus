#include "game/body_contact.h"

#include "game/area_solids.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/play_area.h"
#include "game/world_sounds.h"
#include "physics/body_collision.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace {
// The carts in the world (all but mine when `skip_mine`), stopping people and carts.
void add_carts(std::vector<solid>& solids, const game_state& state, const bool skip_mine) {
    const std::vector<cart_in_world> carts = carts_in_world(state);
    // carts_in_world puts mine first.
    for (std::size_t i = skip_mine ? 1 : 0; i < carts.size(); ++i) {
        solid cart = cart_solid(carts[i].pose, state.tuning.cart, state.tuning.rangefinder.cart_height);
        cart.restitution = state.tuning.solids.cart.restitution;
        cart.friction = state.tuning.solids.cart.friction;
        solids.push_back(cart);
    }
}

// The flagsticks standing in their cups.
void add_flagsticks(std::vector<solid>& solids, const game_state& state) {
    const std::vector<solid> sticks = flagstick_solids(state.area.cups, state.flags_in, state.tuning);
    solids.insert(solids.end(), sticks.begin(), sticks.end());
}

// Other players on foot, stopping carts.
void add_people(std::vector<solid>& solids, const game_state& state) {
    for (const auto& [account, avatar] : state.remote_avatars) {
        if (avatar.mode == motion_mode::walk || avatar.mode == motion_mode::idle) {
            solid person;
            person.kind = solid_kind::person;
            person.base = avatar.position;
            person.radius = state.tuning.player.body_radius;
            person.height = state.tuning.rangefinder.person_height;
            person.stops_cart = true;
            person.restitution = state.tuning.solids.person.restitution;
            person.friction = state.tuning.solids.person.friction;
            solids.push_back(person);
        }
    }
}
}

void keep_walker_clear(game_state& state, const glm::vec3& before) {
    player_state& player = state.player;
    const player_tuning& tuning = state.tuning.player;
    std::vector<solid> moving;
    add_carts(moving, state, false);
    add_flagsticks(moving, state);
    const body_move moved = move_body(body_obstacles{state.area.solids, state.area.solids_grid, moving, solid_mover::walker},
                                      circle_body(tuning.body_radius, 0.0f, tuning.step_up), before, player.position);
    const bool walking_on = horizontal_distance(player.position, before) > 0.0f;
    if (moved.touched && walking_on && !player.against_obstacle) {
        push_audio_event(state, audio_event_type::player_bump, moved.position);
    }
    player.against_obstacle = moved.touched && walking_on;
    player.position = moved.position;
}

std::vector<solid> cart_obstacles(const game_state& state) {
    std::vector<solid> others;
    add_carts(others, state, true);
    add_people(others, state);
    add_flagsticks(others, state);
    return others;
}

void keep_cart_clear(game_state& state, const glm::vec3& before, const std::vector<solid>& others) {
    const cart_tuning& tuning = state.tuning.cart;
    cart_state& cart = state.cart;
    const body_move moved = move_body(body_obstacles{state.area.solids, state.area.solids_grid, others, solid_mover::cart},
                                      cart_body(cart.pose, tuning), before, cart.pose.position);
    cart.against_obstacle = moved.touched;
    cart.pose.position = moved.position;
    // Into what it met it bounces back; along it, it scrapes.
    const float into = -glm::dot(cart.motion.velocity, moved.normal);
    if (into <= 0.0f) {
        return;
    }
    if (into >= tuning.crash_sound_speed) {
        push_audio_event(state, audio_event_type::cart_crash, moved.position);
    }
    const glm::vec2 along = cart.motion.velocity + moved.normal * into;
    cart.motion.velocity = moved.normal * (into * moved.restitution) + along * (1.0f - tuning.scrape_friction);
}

void settle_cart(game_state& state, const float dt, frame_profile* profile) {
    cart_state& cart = state.cart;
    const float gravity = gravity_meters_per_second2 / state.tuning.scale.meters_per_world_unit;
    vertical_body body = cart.vertical;
    body.y = cart.pose.position.y;  // wherever it was put
    cart.vertical = settle_vertical(body, terrain_height(state.area, cart.pose.position, profile), state.tuning.cart.step_up,
                                    gravity, dt);
    cart.pose.position.y = cart.vertical.y;
}
