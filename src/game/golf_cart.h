#pragma once

// My golf cart, a thing in the world: parked beside hole 1 when a course
// starts (park_cart) and left wherever I step out of it. On foot I walk up to
// a seat and interact to step in, and interact again to step out, each step
// taking cart.step_seconds; sat at the wheel I drive it over the ground
// (physics/cart_dynamics.h, by its item's spec). Turned too hard or across
// too steep a slope it tips onto its side and I tumble out beside it; F
// beside it stands it back up over its right_seconds. Driven into water
// deeper than its wade depth it sinks, I wade out, and it is towed back to
// its parking spot after its tow_seconds. Online every player in the room
// has a cart (the server's golf_cart rows), and anyone may ride in
// another's passenger seat: the server checks the reach and that the seat
// is free (board_cart), and the rider sits where the cart is shown here.
// Its driver tells the server when it tips or sinks; anyone may right it.

#include "game/cart_seats.h"
#include "game/game_input.h"
#include "physics/body_collision.h"
#include "physics/cart_dynamics.h"
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
    // Online, how the server's row last had it.
    cart_condition server_condition = cart_condition::upright;
    // Online: the pose came from the server's row of my cart, where it parked it.
    bool placed_by_server = false;
    // Pressed against something standing or a tee box (game/body_contact.h).
    bool against_obstacle = false;
};

// Standing `owner`'s tipped cart back up (0: mine), over its right_seconds.
struct cart_righting {
    std::uint64_t owner = 0;
    float elapsed = 0.0f;
};

// A step into or out of a seat: where it started, and how far into it.
struct cart_step {
    glm::vec3 from{0.0f};
    float from_yaw = 0.0f;
    float elapsed = 0.0f;
    bool getting_in = true;
};

// The seat I sit in, or am stepping into or out of.
struct cart_ride {
    std::uint64_t owner = 0;  // whose cart: 0 is mine, at its wheel
    cart_seat seat = cart_seat::driver;
    std::optional<cart_step> step;
    // Online, in another's cart: the server has had me in its seat.
    bool confirmed = false;
};

// A seat I could step into from here.
struct reachable_seat {
    std::uint64_t owner = 0;  // as cart_ride::owner
    cart_seat seat = cart_seat::driver;
    float distance = 0.0f;    // to the seat (XZ)
};

// A tipped cart I could stand back up from here.
struct reachable_tipped_cart {
    std::uint64_t owner = 0;  // as cart_ride::owner
    float distance = 0.0f;    // to its middle (XZ)
};

// Parks my cart at slot 0 beside hole 1, empty. Online the server parks it
// (play_rules.h free_parking_slot), and follow_server_carts moves it there.
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
// The owner of the cart whose passenger seat the server gives `account`.
std::optional<std::uint64_t> cart_ridden_by(const game_state& state, std::uint64_t account);

// The nearest free seat in cart.board_reach while I walk: my cart's wheel,
// and online any other cart's empty passenger seat.
std::optional<reachable_seat> nearest_seat(const game_state& state);
// Starts stepping into `seat` (online, telling the server).
void step_into_seat(game_state& state, const reachable_seat& seat);

// The nearest tipped cart within cart.board_reach of its middle while I walk.
std::optional<reachable_tipped_cart> nearest_tipped_cart(const game_state& state);
// Starts standing `cart` back up.
void start_righting(game_state& state, const reachable_tipped_cart& cart);
// Every frame: righting goes on while I stay on foot beside the cart, and
// offline a sunk cart of mine is towed back once it has lain its tow_seconds.
void update_carts(game_state& state, float dt);

// While I sit in or step into or out of a seat (state.ride): the step plays,
// interact steps out, and sat I drive my cart or ride where the other is
// shown. Movement XP as on foot, for driving on a road.
void update_cart(game_state& state, const game_input& input, float dt, frame_profile* profile);

// Out of the cart this instant, standing beside the seat when the cart is
// known (a hole starting, a refusal, the cart gone). Online, out of another's
// seat on the server too while it still has me there.
void leave_cart_at_once(game_state& state);

// Online: my cart goes where the server first parks it, stands up again
// when the server has it righted or towed back, and I lose a seat in
// another's cart once the server no longer gives it to me. Offline:
// nothing.
void follow_server_carts(game_state& state);
// Online, when the server refused my cart going over or being righted: it
// stands as the server's row has it.
void adopt_server_cart(game_state& state);

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
};
std::vector<cart_in_world> carts_in_world(const game_state& state);

// My cart's body as it moves (physics/body_collision.h).
ground_body cart_body(const cart_pose& pose, const cart_tuning& tuning);
