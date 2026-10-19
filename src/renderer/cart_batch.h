#pragma once

// The golf cart, built into the world marker batch from flat-coloured,
// opaque pieces: every cart whole, on the ground (game/golf_cart.h
// carts_in_world), mine too while I sit in it and look round inside it. The
// steering wheel is before the driver, who sits `seat_offset` to the left
// of the cart's middle (game/cart_seats.h). GL-free.

#include "game/lighting_loader.h"
#include "renderer/world_marker_batch.h"

#include <cstddef>

#include <glm/vec3.hpp>

// A cart drawn whole: the ground under the middle of its seat, facing along
// `yaw` (see yaw_direction in physics/vector_math.h).
struct render_cart {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    bool driven = false;  // someone at its wheel: its headlights are on
    float steer = 0.0f;   // its steering wheel's turn, -1 to 1, positive to the left
};

void append_cart_model(world_marker_batch& batch, const render_cart& cart, float seat_offset, const cart_colors& colors);
std::size_t cart_vertex_count();
