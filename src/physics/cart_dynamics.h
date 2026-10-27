#pragma once

// A golf cart on the ground, one fixed step at a time (cart_step_seconds),
// so a frame's length never changes where it ends up. Along its heading an
// electric motor pushes with at most max_drive_force_n and motor_power_w,
// against gravity along the slope and the rolling resistance of what is
// under its wheels, and a governor holds it to its top speed either way
// (motor braking downhill). Its steering turns it as far as the tyres' grip
// allows; past it the cart slides on along its velocity. Its uphill wheels
// lift when the turn's pull outward and the side slope's together beat its
// static stability factor (SSF = track / (2 cg height), NHTSA), and held
// lifted for tip_seconds it rolls onto its side. In water deeper than
// wade_depth_m it sinks. Metres and seconds, seen from above. Pure.

#include <cstdint>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

inline constexpr float cart_step_seconds = 1.0f / 120.0f;

// A cart's build (its item's `spec`, assets/play/items/cart/), with two aboard.
struct cart_spec {
    float mass_kg = 0.0f;
    float motor_power_w = 0.0f;
    float max_drive_force_n = 0.0f;
    float reverse_share = 0.0f;  // of the force and the top speed, backing up
    float top_speed_mps = 0.0f;
    float wheelbase_m = 0.0f;
    float track_m = 0.0f;
    float cg_height_m = 0.0f;
    float max_steer_degrees = 0.0f;  // the front wheels at full lock
    float tip_seconds = 0.0f;        // its uphill wheels lifted this long and it tips
    float wade_depth_m = 0.0f;       // water deeper than this under its middle sinks it
    float right_seconds = 0.0f;      // standing a tipped cart back up
    float tow_seconds = 0.0f;        // a sunk cart is back at its parking spot after this
};

// Upright on its wheels, lying on one side, or sunk.
enum class cart_condition : std::uint8_t {
    upright,
    on_left_side,
    on_right_side,
    sunk
};

inline bool cart_tipped(const cart_condition condition) {
    return condition == cart_condition::on_left_side || condition == cart_condition::on_right_side;
}

// What the wheels are on, under the cart's middle.
struct cart_ground {
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    float rolling_resistance = 0.0f;  // Crr
    float grip = 0.0f;                // the tyres' friction coefficient
    float water_depth = 0.0f;         // metres of water over the ground
    bool on_ground = true;            // false: in the air off an edge
};

// The driver: the pedal (forward positive, backwards negative; against the
// way it rolls it brakes) and the front wheels' turn as a share of full
// lock, positive to the left. With no pedal the motor brakes it and holds it
// standing (an electric cart's park brake).
struct cart_controls {
    float throttle = 0.0f;
    float steer = 0.0f;
};

struct cart_motion {
    float yaw = 0.0f;  // see yaw_direction in physics/vector_math.h
    glm::vec2 velocity{0.0f};  // m/s, world XZ
    float yaw_rate = 0.0f;     // rad/s, positive to the left
    float lift_seconds = 0.0f;  // how long its uphill wheels have been off the ground
    cart_condition condition = cart_condition::upright;
    float load = 0.0f;  // the share of max_drive_force_n the motor drives with
};

// Its speed along its heading, negative backing up.
float cart_forward_speed(const cart_motion& motion);

// Its static stability factor: the side pull, in g, that lifts its wheels on the flat.
float cart_stability(const cart_spec& spec);

// `in` one step of `dt` on `ground` as the driver has it. Tipped or sunk it
// stays where it is.
cart_motion step_cart(const cart_motion& in, const cart_controls& controls, const cart_ground& ground, const cart_spec& spec,
                      float dt);
