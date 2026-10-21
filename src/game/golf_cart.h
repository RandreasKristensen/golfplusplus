#pragma once

// My golf cart, a thing in the world: parked beside hole 1 when a course
// starts (park_cart) and left wherever I step out of it. On foot I walk up to
// a seat and interact to step in, and interact again to step out, each step
// taking cart.step_seconds; sat at the wheel I drive it. Online every player
// in the room has a cart (the server's golf_cart rows), and anyone may ride
// in another's passenger seat: the server checks the reach and that the
// seat is free (board_cart), and the rider sits where the cart is shown here.

#include "game/cart_seats.h"
#include "game/game_input.h"
#include "profiling/profiling.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

struct game_state;

// My cart where it stands, and its speed along its yaw (negative backing up).
struct cart_state {
    cart_pose pose;
    float velocity = 0.0f;
    // Its steering wheel's turn, easing after the steering: -1 to 1,
    // positive to the left. Looks only.
    float steer = 0.0f;
    // Online: the pose came from the server's row of my cart, where it parked it.
    bool placed_by_server = false;
    // Pressed against something standing or a tee box (game/body_contact.h).
    bool against_obstacle = false;
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

// Parks my cart at slot 0 beside hole 1, empty. Online the server parks it
// (play_rules.h free_parking_slot), and follow_server_carts moves it there.
void park_cart(game_state& state);

// Not sat in a cart nor stepping into or out of one.
bool on_foot(const game_state& state);
// Sat at my cart's wheel.
bool driving_cart(const game_state& state);

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

// While I sit in or step into or out of a seat (state.ride): the step plays,
// interact steps out, and sat I drive my cart or ride where the other is
// shown. Movement XP as on foot, for driving on a road.
void update_cart(game_state& state, const game_input& input, float dt, frame_profile* profile);

// Out of the cart this instant, standing beside the seat when the cart is
// known (a hole starting, a refusal, the cart gone). Online, out of another's
// seat on the server too while it still has me there.
void leave_cart_at_once(game_state& state);

// Online: my cart goes where the server first parks it, and I lose a seat
// in another's cart once the server no longer gives it to me. Offline:
// nothing.
void follow_server_carts(game_state& state);

// A cart drawn whole in the world: mine, and everyone else's in my room.
// `driven`: someone sits at its wheel, and its headlights are on; `steer`
// is its steering wheel's turn (as cart_state::steer).
struct cart_in_world {
    cart_pose pose;
    bool driven = false;
    float steer = 0.0f;
};
std::vector<cart_in_world> carts_in_world(const game_state& state);
