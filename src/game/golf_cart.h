#pragma once

// My golf cart, a thing in the world: parked beside hole 1 when a course
// starts with one taken (park_cart) and left wherever I step out of it. On
// foot I walk into a seat's door to step in (a moment's deliberate push, so
// brushing past or reaching for my bag never seats me), and F steps out,
// each step taking cart.step_seconds; through the other seat's door I scoot
// across to mine. Sat at the wheel I drive it over the ground
// (physics/cart_dynamics.h, by its item's spec). Turned too hard or across
// too steep a slope it rolls over onto its side (physics/cart_rollover.h)
// and I tumble out beside it; F beside it stands it back up over its
// right_seconds. Driven into water deeper than its wade depth it sinks, I
// wade out, and it is towed back to its parking spot after its tow_seconds.
// Online a cart exists only as its golf_cart row. Outside confirmed
// driving the row owns its pose and condition; take_wheel confirms my
// wheel before movement. Anyone may ride in another's passenger seat: the
// server checks the reach and that the seat is free (board_cart), and the
// rider sits where the cart is shown here, carried by its driver's
// ride_motion (game/ride_sync.h). Its driver tells the server when it tips
// or sinks; anyone may right it.

#include "game/cart_seats.h"
#include "game/game_input.h"
#include "physics/body_collision.h"
#include "physics/cart_dynamics.h"
#include "physics/cart_rollover.h"
#include "physics/solid.h"
#include "profiling/profiling.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct game_state;

// My cart where it stands and how it moves; its motion's yaw is the pose's.
struct cart_state {
    cart_pose pose;
    cart_motion motion;
    // Up and down: on its wheels, or in the air off an edge.
    vertical_body vertical;
    // Its steering wheel's turn, following A and D at cart.steering_wheel_rate:
    // -1 to 1, positive to the left, the front wheels' share of full lock.
    float steer = 0.0f;
    // Of the frames' time, what is not stepped yet (cart_step_seconds).
    float unstepped_seconds = 0.0f;
    // Offline, how long it has lain sunk.
    float sunk_seconds = 0.0f;
    // Pressed against something standing or a tee box (game/body_contact.h).
    bool against_obstacle = false;
    glm::vec3 exit_correction{0.0f};
    float exit_yaw_correction = 0.0f;
    float exit_correction_left = 0.0f;
};

// Standing `owner`'s tipped cart back up (0: mine), over its right_seconds.
struct cart_righting {
    std::uint64_t owner = 0;
    float elapsed = 0.0f;
};

// A step into or out of a seat: where it started, and how far into it.
// Getting in through the other seat's door, into that seat first and then
// across (cart.scoot_seconds).
struct cart_step {
    glm::vec3 from{0.0f};
    float from_yaw = 0.0f;
    float elapsed = 0.0f;
    bool getting_in = true;
    cart_seat entered = cart_seat::driver;
};

// The seat I sit in, or am stepping into or out of.
struct cart_ride {
    std::uint64_t owner = 0;  // whose cart: 0 is mine, at its wheel
    cart_seat seat = cart_seat::driver;
    std::optional<cart_step> step;
    // Online: the server has had me in this seat, including my wheel.
    bool confirmed = false;
};

// A cart seat's door I stand at: whose cart, which side's door, and the
// seat it takes me to (at my own cart the driver's, at another's the
// passenger's, scooting across from the other door).
struct seat_door {
    std::uint64_t owner = 0;  // as cart_ride::owner
    cart_seat door = cart_seat::driver;
    cart_seat seat = cart_seat::driver;
};

// Walking into a door, and for how long.
struct door_push {
    seat_door door;
    float seconds = 0.0f;
};

// A cart as shown going over: the condition last seen (a fall starts when
// it is seen tipping), and its fall onto its side.
struct cart_fall_shown {
    cart_condition seen = cart_condition::upright;
    cart_fall fall;
    float unstepped = 0.0f;  // of the frames' time, not stepped yet
};

// A tipped cart I could stand back up from here.
struct reachable_tipped_cart {
    std::uint64_t owner = 0;  // as cart_ride::owner
    float distance = 0.0f;    // to its middle (XZ)
};

// Whether I have a cart on this course (course_definition::carts): without
// one it is nowhere, drawn, ridden, bumped into or carrying my bag.
bool own_cart(const game_state& state);

// Offline parks at slot 0 beside hole 1. Online resets prediction and
// adopts my row if it has arrived; no provisional cart is parked.
void park_cart(game_state& state);

// Not sat in a cart nor stepping into or out of one.
bool on_foot(const game_state& state);
// Sat at my cart's wheel.
bool driving_cart(const game_state& state);

// The build of the cart `item` (a cart item, game/item_catalog.h); my
// cart's with what items do to it now (cart.top_speed, cart.uphill). Real
// numbers: the game runs them cart.time_scale faster.
const cart_spec& cart_spec_of(const game_state& state, const std::string& item);
cart_spec my_cart_spec(const game_state& state);
// My cart's speed along its heading, and its top speed, in the game's world
// units a second (negative backing up).
float my_cart_speed(const game_state& state);
float my_cart_top_speed(const game_state& state);
// How fast its motor's loop plays: its speed's share of its top speed and
// the share of its force it drives with (cart.motor_*_pitch).
float motor_pitch(const game_state& state);
float motor_pitch_at(const cart_tuning& tuning, float speed_share, float load);

// Where `owner`'s cart is shown (0 or my account: mine). Another's is where
// its row has it, or while they drive it, round their shown figure.
std::optional<cart_pose> shown_cart(const game_state& state, std::uint64_t owner);
// Whether `owner`'s cart (0 or my account: mine) stands on its wheels.
cart_condition cart_condition_of(const game_state& state, std::uint64_t owner);
// The owner of the cart whose passenger seat the server gives `account`.
std::optional<std::uint64_t> cart_ridden_by(const game_state& state, std::uint64_t account);

// The seat door I stand at on foot (cart.door_*): my cart's, and online
// any other's standing on its wheels.
std::optional<seat_door> seat_door_at(const game_state& state);
// Walking on foot: walking into the door I stand at for
// cart.door_push_seconds steps into its seat (online, telling the server);
// a seat taken (the passenger's, or the driver's through their door) says
// so with a notice instead.
void walk_into_seat(game_state& state, const game_input& input, float dt);

// The nearest tipped cart within cart.board_reach of its middle while I walk.
std::optional<reachable_tipped_cart> nearest_tipped_cart(const game_state& state);
// Starts standing `cart` back up.
void start_righting(game_state& state, const reachable_tipped_cart& cart);
// Every frame: righting goes on while I stay on foot beside the cart, a
// cart seen going over falls onto its side (state.cart_falls), and offline
// a sunk cart of mine is towed back once it has lain its tow_seconds, my
// bag still strapped on it set down where it went in.
void update_carts(game_state& state, float dt);

// While I sit in or step into or out of a seat (state.ride): the step plays,
// interact steps out, and sat I drive my cart or ride where the other is
// shown. Movement XP as on foot, for driving on a road.
void update_cart(game_state& state, const game_input& input, float dt, frame_profile* profile);

// Out of the cart this instant, standing beside the seat when the cart is
// known (a hole starting, a refusal, the cart gone). Online, out of another's
// seat on the server too while it still has me there.
void leave_cart_at_once(game_state& state);

// Online: adopts my row whenever I am not driving and confirms or removes
// seats from the rows. Offline does nothing.
void follow_server_carts(game_state& state);
// Online, when the server refused my cart going over or being righted: it
// stands as the server's row has it.
void adopt_server_cart(game_state& state);

// The same fixed step used while driving and when replaying unacknowledged inputs.
void step_predicted_cart(game_state& state, const cart_controls& controls, const cart_spec& spec, float grip_scale,
                         const std::vector<solid>& others, frame_profile* profile);

// A cart drawn whole in the world: mine, and everyone else's in my room.
// `driven`: someone sits at its wheel, and its headlights are on; `steer`
// is its steering wheel's turn (as cart_state::steer).
struct cart_in_world {
    std::uint64_t owner = 0;  // 0: mine
    cart_pose pose;
    cart_condition condition = cart_condition::upright;
    bool driven = false;
    float steer = 0.0f;
    std::string item;  // its owner's cart item (game/loadout.h)
    float speed = 0.0f;  // along its heading as driven, world units a second
    float fall_angle = 0.0f;  // tipped: how far over onto its side (physics/cart_rollover.h)
};
std::vector<cart_in_world> carts_in_world(const game_state& state);
