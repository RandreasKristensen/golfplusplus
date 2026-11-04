#include "game/movement_step.h"

#include "game/area_solids.h"
#include "game/play_area.h"
#include "game/player_jump.h"
#include "physics/det_math.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr float yaw_steps = 65536.0f;
constexpr float steer_steps = 32767.0f;

solid contact_solid(solid thing, const solid_contact_tuning& contact) {
    thing.restitution = contact.restitution;
    thing.friction = contact.friction;
    return thing;
}

void add_carts(std::vector<solid>& solids, const std::vector<cart_pose>& carts, const game_tuning& tuning) {
    for (const cart_pose& pose : carts) {
        solids.push_back(contact_solid(cart_solid(pose, tuning.cart, tuning.rangefinder.cart_height), tuning.solids.cart));
    }
}

void add_flagsticks(std::vector<solid>& solids, const std::vector<cup>& cups, const std::vector<bool>& flags_in,
                    const game_tuning& tuning) {
    const std::vector<solid> sticks = flagstick_solids(cups, flags_in, tuning);
    solids.insert(solids.end(), sticks.begin(), sticks.end());
}

bool standing_still(const walker& in, const move_input& input) {
    return input.forward == 0 && input.side == 0 && !input.jump && in.vertical.grounded && in.vertical.speed == 0.0f;
}
}

void append_step(std::vector<move_run>& runs, const move_input& input) {
    if (!runs.empty() && !input.jump && !runs.back().input.jump && runs.back().input == input &&
        runs.back().steps < UINT16_MAX) {
        ++runs.back().steps;
        return;
    }
    runs.push_back(move_run{input, 1});
}

std::uint16_t quantise_yaw(const float yaw) {
    const float pi = glm::pi<float>();
    const long step = std::lround((wrap_angle(yaw) + pi) / glm::two_pi<float>() * yaw_steps);
    return static_cast<std::uint16_t>(step & 0xFFFF);
}

float yaw_of(const std::uint16_t yaw) {
    return static_cast<float>(yaw) * (glm::two_pi<float>() / yaw_steps) - glm::pi<float>();
}

std::int16_t quantise_steer(const float steer) {
    return static_cast<std::int16_t>(std::lround(std::clamp(steer, -1.0f, 1.0f) * steer_steps));
}

float steer_of(const std::int16_t steer) {
    return std::clamp(static_cast<float>(steer) / steer_steps, -1.0f, 1.0f);
}

walk_step step_walker(const walker& in, const move_input& input, const walk_world& world) {
    walk_step out{in, false};
    walker& after = out.after;
    after.speed = 0.0f;
    after.move_angle = 0.0f;
    // Standing on the ground with nothing pressed, nothing moves.
    if (standing_still(in, input)) {
        after.against_obstacle = false;
        return out;
    }
    const player_tuning& tuning = world.tuning;
    const float forward = static_cast<float>(std::clamp<int>(input.forward, -1, 1));
    const float side = static_cast<float>(std::clamp<int>(input.side, -1, 1));
    if (forward != 0.0f || side != 0.0f) {
        const float sign = forward < 0.0f ? -1.0f : 1.0f;
        after.move_angle = det_atan2(side * sign, forward * sign);
        const float speed = input.run ? world.walk_speed * tuning.run_factor : world.walk_speed;
        after.speed = speed * sign;
        after.position += yaw_direction(yaw_of(input.yaw) + after.move_angle) * (after.speed * move_step_seconds);
    }
    const body_move moved = move_body(body_obstacles{world.area.solids, world.area.solids_grid, world.moving, solid_mover::walker},
                                      circle_body(tuning.body_radius, 0.0f, tuning.step_up), in.position, after.position);
    const bool walking_on = horizontal_distance(after.position, in.position) > 0.0f;
    out.bumped = moved.touched && walking_on && !in.against_obstacle;
    after.against_obstacle = moved.touched && walking_on;
    after.position = moved.position;
    const vertical_body taking_off = input.jump ? hopped(in.vertical, tuning) : in.vertical;
    after.vertical = settle_on_foot(taking_off, after.position.y, terrain_height(world.area, after.position, world.profile),
                                    tuning, move_step_seconds);
    after.position.y = after.vertical.y;
    return out;
}

std::vector<solid> walker_obstacles(const std::vector<cart_pose>& carts, const std::vector<cup>& cups,
                                    const std::vector<bool>& flags_in, const game_tuning& tuning) {
    std::vector<solid> solids;
    add_carts(solids, carts, tuning);
    add_flagsticks(solids, cups, flags_in, tuning);
    return solids;
}

cart_ground cart_ground_under(const cart_pose& pose, const cart_world& world) {
    const game_tuning& tuning = world.tuning;
    cart_ground ground = cart_ground_at(world.area, tuning, world.wetness, pose.position, world.profile);
    const glm::vec3 forward = yaw_direction(pose.yaw);
    const glm::vec3 left(forward.z, 0.0f, -forward.x);
    const float unit = tuning.scale.meters_per_world_unit;
    const glm::vec3 middle = pose.position + forward * (0.5f * (tuning.cart.body_front - tuning.cart.body_back));
    const glm::vec3 half_base = forward * (0.5f * world.spec.wheelbase_m / unit);
    const glm::vec3 half_track = left * (0.5f * world.spec.track_m / unit);
    float grip = 0.0f;
    float rolling_resistance = 0.0f;
    for (const glm::vec3& wheel : {middle + half_base + half_track, middle + half_base - half_track,
                                   middle - half_base + half_track, middle - half_base - half_track}) {
        const cart_ground under = cart_ground_at(world.area, tuning, world.wetness, wheel, world.profile);
        grip += 0.25f * under.grip;
        rolling_resistance += 0.25f * under.rolling_resistance;
    }
    ground.grip = grip;
    ground.rolling_resistance = rolling_resistance;
    return ground;
}

cart_move step_cart_mover(const cart_mover& in, const cart_controls& controls, const cart_world& world) {
    const game_tuning& tuning = world.tuning;
    cart_move out{in, 0.0f};
    cart_mover& cart = out.after;
    cart.motion.yaw = cart.pose.yaw;
    cart_ground ground = cart_ground_under(cart.pose, world);
    ground.grip *= world.grip_scale;
    ground.on_ground = cart.vertical.grounded;
    const float cart_seconds = move_step_seconds * tuning.cart.time_scale;
    cart.motion = step_cart(cart.motion, controls, ground, world.spec, cart_seconds);
    cart.pose.yaw = cart.motion.yaw;
    if (cart.motion.condition != cart_condition::upright) {
        return out;
    }
    const glm::vec3 from = cart.pose.position;
    const glm::vec2 travel = cart.motion.velocity * (cart_seconds / tuning.scale.meters_per_world_unit);
    cart.pose.position += glm::vec3(travel.x, 0.0f, travel.y);

    const body_move moved = move_body(body_obstacles{world.area.solids, world.area.solids_grid, world.others, solid_mover::cart},
                                      cart_body(cart.pose, tuning.cart), from, cart.pose.position);
    cart.against_obstacle = moved.touched;
    cart.pose.position = moved.position;
    // Into what it met it bounces back; along it, it scrapes.
    const float into = -glm::dot(cart.motion.velocity, moved.normal);
    if (into > 0.0f) {
        out.crash_speed = into;
        const glm::vec2 along = cart.motion.velocity + moved.normal * into;
        cart.motion.velocity = moved.normal * (into * moved.restitution) + along * (1.0f - tuning.cart.scrape_friction);
    }

    const float gravity = gravity_meters_per_second2 / tuning.scale.meters_per_world_unit;
    vertical_body body = cart.vertical;
    body.y = cart.pose.position.y;
    cart.vertical = settle_vertical(body, terrain_height(world.area, cart.pose.position, world.profile), tuning.cart.step_up,
                                    gravity, move_step_seconds);
    cart.pose.position.y = cart.vertical.y;
    return out;
}

std::vector<solid> cart_obstacles_of(const std::vector<cart_pose>& carts, const std::vector<glm::vec3>& people,
                                     const std::vector<cup>& cups, const std::vector<bool>& flags_in,
                                     const game_tuning& tuning) {
    std::vector<solid> solids;
    add_carts(solids, carts, tuning);
    for (const glm::vec3& feet : people) {
        solid person;
        person.kind = solid_kind::person;
        person.base = feet;
        person.radius = tuning.player.body_radius;
        person.height = tuning.rangefinder.person_height;
        person.stops_cart = true;
        solids.push_back(contact_solid(person, tuning.solids.person));
    }
    add_flagsticks(solids, cups, flags_in, tuning);
    return solids;
}

ground_body cart_body(const cart_pose& pose, const cart_tuning& tuning) {
    const glm::vec3 forward = yaw_direction(pose.yaw);
    return box_body(glm::vec2(forward.x, forward.z), 0.5f * (tuning.body_front + tuning.body_back), tuning.body_half_width,
                    0.5f * (tuning.body_front - tuning.body_back), tuning.scrape_friction, tuning.step_up);
}

cart_spec effective_cart_spec(cart_spec spec, const std::vector<active_effect>& effects, const std::int64_t now_ms) {
    spec.top_speed_mps = effective_value(spec.top_speed_mps, effect_target_cart_top_speed, effects, now_ms);
    spec.max_drive_force_n = effective_value(spec.max_drive_force_n, effect_target_cart_uphill, effects, now_ms);
    return spec;
}

float cart_grip_scale(const std::vector<active_effect>& effects, const std::int64_t now_ms) {
    return effective_value(1.0f, effect_target_cart_drift, effects, now_ms);
}
