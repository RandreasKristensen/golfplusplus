#pragma once

// The golf cart, built into the world marker batch from flat-coloured,
// opaque pieces: every cart whole, on the ground (game/golf_cart.h
// carts_in_world), mine too while I sit in it and look round inside it. It
// tilts with the ground under its wheels and stands a little clear of it
// (cart_on_ground). The steering wheel is before the driver, who sits
// `seat_offset` to the left of the cart's middle (game/cart_seats.h). GL-free.

#include "game/lighting_loader.h"
#include "renderer/world_marker_batch.h"

#include <cstddef>
#include <functional>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// A cart drawn whole: under the middle of its seat at the bottom of its
// wheels, facing along `yaw` (see yaw_direction in physics/vector_math.h),
// its roof towards `up`.
struct render_cart {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    bool driven = false;  // someone at its wheel: its headlights are on
    float steer = 0.0f;   // its steering wheel's turn, -1 to 1, positive to the left
    glm::vec3 up{0.0f, 1.0f, 0.0f};
};

// The cart's frame in the world: x towards its left, y towards its roof,
// z forward, at its position.
glm::mat4 cart_frame(const render_cart& cart);

// The cart under the middle of its seat at `position` (XZ), facing `yaw`,
// tilted to the ground under its four wheels and lifted so no tyre is
// nearer the ground than a small clearance.
render_cart cart_on_ground(const glm::vec3& position, float yaw,
                           const std::function<float(const glm::vec3&)>& ground_height);

void append_cart_model(world_marker_batch& batch, const render_cart& cart, float seat_offset, const cart_colors& colors);
std::size_t cart_vertex_count();
