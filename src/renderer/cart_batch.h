#pragma once

// The golf cart, its model (a `cart` model in assets/world/props/) drawn
// into the prop batch: every cart whole, on the ground (game/golf_cart.h
// carts_in_world), mine too while I sit in it and look round inside it, a
// sunk one on its pond's bed. It
// tilts with the ground under its wheels and stands a little clear of it
// (cart_on_ground). Its wheels and steering wheel turn, at its model's
// points. GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>

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
    std::string model;    // its cart item's model
    std::map<std::string, std::string> bags;  // a mount of its model -> the bag's model strapped there (bag_batch.h)
};

// The cart's frame in the world: x towards its left, y towards its roof,
// z forward, at its position.
glm::mat4 cart_frame(const render_cart& cart);

// The cart of `model` under the middle of its seat at `position` (XZ),
// facing `yaw`, tilted to the ground under its four wheels and lifted so no
// tyre is nearer the ground than a small clearance.
render_cart cart_on_ground(const glm::vec3& position, float yaw, const prop_model& model,
                           const std::function<float(const glm::vec3&)>& ground_height);

// The cart of `model` that stood at `position` facing `yaw`, rolled over
// its left (`on_left_side`) or right wheels onto that side: its roof
// towards that side, lying on the ground at `ground`'s height.
render_cart cart_on_side(const glm::vec3& position, float yaw, bool on_left_side, const prop_model& model, float ground);

void append_cart_model(prop_batch& batch, const render_cart& cart, const prop_model& model, const glow_floors& glow);
std::size_t cart_vertex_count(const prop_model& model);
