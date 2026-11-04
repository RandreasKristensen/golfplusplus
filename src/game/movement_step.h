#pragma once

// One fixed step of walking and of driving, played alike by the game and by
// the server, which replays each player's inputs with it (online movement is
// by input: docs/networking_rules.md). Pure and on det_math, so the native
// game and the server's WASM step bit for bit. Every step is
// move_step_seconds of real time; a cart's physics steps cart.time_scale
// times as much cart time. Inputs are quantised as they are sent
// (move_input), and the game steps the quantised values too.

#include "game/cart_seats.h"
#include "game/effects.h"
#include "game/game_tuning.h"
#include "physics/body_collision.h"
#include "physics/cart_dynamics.h"
#include "physics/cup.h"
#include "physics/solid.h"
#include "profiling/profiling.h"

#include <cstdint>
#include <vector>

#include <glm/vec3.hpp>

struct play_area;

inline constexpr float move_step_seconds = cart_step_seconds;

// What a step is: on foot, or at my cart's wheel.
enum class move_kind : std::uint8_t {
    walk,
    cart
};

// One step's controls. On foot: W/S and A/D (+1, -1 or 0; `side` positive
// left), Shift, a hop (Space) and where I face; at the wheel the pedal
// (`forward`) and the steering wheel's turn.
struct move_input {
    move_kind kind = move_kind::walk;
    std::int8_t forward = 0;
    std::int8_t side = 0;
    bool run = false;
    bool jump = false;
    std::uint16_t yaw = 0;    // quantise_yaw
    std::int16_t steer = 0;   // quantise_steer
};

inline bool operator==(const move_input& a, const move_input& b) {
    return a.kind == b.kind && a.forward == b.forward && a.side == b.side && a.run == b.run && a.jump == b.jump &&
        a.yaw == b.yaw && a.steer == b.steer;
}

// Steps in a row with the same input, as they are sent. A hop is always a
// run of its own one step.
struct move_run {
    move_input input;
    std::uint16_t steps = 0;
};

// Appends one step of `input` to `runs`.
void append_step(std::vector<move_run>& runs, const move_input& input);

// A yaw in [-pi, pi) to 1/65536 of a turn, and back; a steering wheel turn
// (-1..1) to 1/32767, and back.
std::uint16_t quantise_yaw(float yaw);
float yaw_of(std::uint16_t yaw);
std::int16_t quantise_steer(float steer);
float steer_of(std::int16_t steer);

// --- On foot ---------------------------------------------------------------

// Me on foot: my feet, up and down (game/player_jump.h), and how I moved
// on the last step: speed along my facing turned by move_angle (negative
// walking backwards), and whether I pressed against something.
struct walker {
    glm::vec3 position{0.0f};
    vertical_body vertical;
    float speed = 0.0f;
    float move_angle = 0.0f;  // in [-pi/2, pi/2], positive left
    bool against_obstacle = false;
};

struct walk_world {
    const play_area& area;
    const std::vector<solid>& moving;  // walker_obstacles
    const player_tuning& tuning;
    float walk_speed = 0.0f;  // player.walk_speed with what items do to it now
    frame_profile* profile = nullptr;
};

struct walk_step {
    walker after;
    bool bumped = false;  // walked into something this step, from free
};

// W and S walk where I face, A and D step sideways, at walking speed in any
// direction, run_factor times it running; backwards I keep facing forward.
// Kept clear of what stands in my way; a hop leaves the ground, and off an
// edge higher than a step I fall.
walk_step step_walker(const walker& in, const move_input& input, const walk_world& world);

// What stops me on foot besides the area's solids: every cart (`carts`)
// and the flagsticks in their cups.
std::vector<solid> walker_obstacles(const std::vector<cart_pose>& carts, const std::vector<cup>& cups,
                                    const std::vector<bool>& flags_in, const game_tuning& tuning);

// --- At the wheel ----------------------------------------------------------

// What a cart's fixed step moves; its motion's yaw is the pose's.
struct cart_mover {
    cart_pose pose;
    cart_motion motion;
    vertical_body vertical;
    bool against_obstacle = false;
};

struct cart_world {
    const play_area& area;
    const game_tuning& tuning;
    const cart_spec& spec;  // effective_cart_spec
    float wetness = 0.0f;
    float grip_scale = 1.0f;  // cart_grip_scale
    const std::vector<solid>& others;  // cart_obstacles_of
    frame_profile* profile = nullptr;
};

struct cart_move {
    cart_mover after;
    float crash_speed = 0.0f;  // m/s into what it hit this step, 0 for nothing
};

// What the cart at `pose` drives on: the grip and rolling resistance under
// each of its four wheels (its spec's wheelbase and track about its body's
// middle), averaged, so it takes an edge onto or off a path one wheel at a
// time rather than all at once; the slope and water under its seat.
cart_ground cart_ground_under(const cart_pose& pose, const cart_world& world);

// One step over the ground at `controls`, clear of what stands in its way
// (bouncing off what it crashes into, scraping along it), on the surface or
// falling onto it.
cart_move step_cart_mover(const cart_mover& in, const cart_controls& controls, const cart_world& world);

// What stops a cart besides the area's solids: the other carts, people on
// foot (their feet) and the flagsticks in their cups.
std::vector<solid> cart_obstacles_of(const std::vector<cart_pose>& carts, const std::vector<glm::vec3>& people,
                                     const std::vector<cup>& cups, const std::vector<bool>& flags_in,
                                     const game_tuning& tuning);

// A cart's body as it moves (physics/body_collision.h).
ground_body cart_body(const cart_pose& pose, const cart_tuning& tuning);

// A cart's build with what items do to it now (cart.top_speed, cart.uphill),
// and its tyres' grip (cart.drift).
cart_spec effective_cart_spec(cart_spec spec, const std::vector<active_effect>& effects, std::int64_t now_ms);
float cart_grip_scale(const std::vector<active_effect>& effects, std::int64_t now_ms);
