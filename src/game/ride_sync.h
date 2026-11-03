#pragma once

// Riding together online. The room hears a driver's motion sparsely
// (game/motion_sync.h): enough to watch a cart go by, too little to sit in.
// So while someone rides in my cart I also send my seat at its wheel to the
// server every net.ride_motion_interval_seconds (update_ride), which keeps
// it in a ride_motion row only my passenger follows. Riding in another's
// cart I am carried on that: their cart as their latest ride motion has it,
// carried on along it to now, a new one blending in over
// net.ride_correction_seconds. Presentation only: where the cart is stays
// the driver's word (their room motion and the golf_cart row).

#include "game/cart_seats.h"

#include <cstdint>
#include <optional>

#include <glm/vec3.hpp>

struct game_state;

// The cart I ride in as shown, from its driver's ride motions.
struct ride_view {
    std::uint64_t owner = 0;
    std::int64_t motion_at = 0;  // the ride motion followed (server time)
    cart_pose shown;
    glm::vec3 correction{0.0f};  // shown minus followed when that motion arrived
    float yaw_correction = 0.0f;
    float correction_left = 0.0f;  // seconds of the blend still to run
};

// Online, at my wheel with someone in my passenger seat: my motion goes to
// them every ride interval. Otherwise nothing.
void sync_ride_motion(game_state& state, float dt);

// Online, sat in another's cart: follows its ride motions into
// state.ride_shown, or forgets them when there are none for that cart.
void follow_ride(game_state& state, float dt);
