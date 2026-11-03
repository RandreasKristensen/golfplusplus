#pragma once

// The golf cart, its model (a `cart` model in assets/world/props/) drawn
// into the prop batch: every cart whole, on the ground (game/golf_cart.h
// carts_in_world), mine too while I sit in it and look round inside it, a
// sunk one on its pond's bed. It
// tilts with the ground under its wheels and stands a little clear of it
// (cart_on_ground); going over it turns about its wheels on that side onto
// its side, its lowest point always on the ground (cart_rolled). Its body
// wears its owner's paint (game/gear_paint.h). Its wheels and steering
// wheel turn, at its model's points. GL-free.

#include "game/gear_paint.h"
#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// A bag strapped on a cart: its model, in its owner's bag paint.
struct render_strapped_bag {
    std::string model;
    gear_paint paint;
};

// A cart drawn whole: under the middle of its seat at the bottom of its
// wheels, facing along `yaw` (see yaw_direction in physics/vector_math.h),
// its roof towards `up`; or, given, its front towards `forward` (square to `up`).
struct render_cart {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    bool driven = false;  // someone at its wheel: its headlights are on
    float steer = 0.0f;   // its steering wheel's turn, -1 to 1, positive to the left
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    std::optional<glm::vec3> forward;
    std::string model;    // its cart item's model
    gear_paint paint;  // its owner's
    std::map<std::string, render_strapped_bag> bags;  // a mount of its model -> the bag strapped there (bag_batch.h)
};

// The cart's frame in the world: x towards its left, y towards its roof,
// z forward, at its position.
glm::mat4 cart_frame(const render_cart& cart);

// The cart of `model` under the middle of its seat at `position` (XZ),
// facing `yaw`, tilted to the ground under its four wheels and lifted so no
// tyre is nearer the ground than a small clearance.
render_cart cart_on_ground(const glm::vec3& position, float yaw, const prop_model& model,
                           const std::function<float(const glm::vec3&)>& ground_height);

// The cart of `model` standing at `position` facing `yaw` (as
// cart_on_ground), turned `angle` (radians, physics/cart_rollover.h) about
// the line its left (`on_left_side`) or right wheels stand on, towards that
// side: a quarter turn lies it on that side. Then raised or lowered until
// the lowest of its body's corners and its tyres' edges is on the ground.
render_cart cart_rolled(const glm::vec3& position, float yaw, bool on_left_side, float angle, const prop_model& model,
                        const std::function<float(const glm::vec3&)>& ground_height);

// In `colors`' cart paint for its owner.
void append_cart_model(prop_batch& batch, const render_cart& cart, const prop_model& model, const scene_colors& colors);
// A golfer's bag and cart paint colours.
glm::vec3 bag_paint_color(const figure_colors& colors, const gear_paint& paint);
glm::vec3 cart_paint_color(const figure_colors& colors, const gear_paint& paint);
std::size_t cart_vertex_count(const prop_model& model);
