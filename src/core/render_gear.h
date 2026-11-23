#pragma once

// Carts and bags in a frame (core/render_frame.h): every cart, standing,
// going over or lying on its side (physics/cart_rollover.h), each in its
// owner's paint (game/gear_paint.h) with the bags strapped on it, my bag set
// down, and a bag or club being handled (game/bag_handling.h), mine or
// another's, on its way with the golfer's figure reaching for it. GL-free.

#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/prop_loader.h"
#include "renderer/render_data.h"

#include <cstdint>
#include <string>
#include <vector>

// How each cart sat on the ground in the last frame: a cart that has not
// moved sits as it did, so its wheels' ground queries are not made again.
// Each remembers what its fit was for; a change in any of it fits it again.
struct cart_fit_memo {
    struct entry {
        std::uint64_t owner = 0;
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        cart_condition condition = cart_condition::upright;
        float fall_angle = 0.0f;
        std::string model;
        render_cart fit;
    };
    std::vector<entry> entries;
    std::uint64_t revision = 0;  // game_state::static_anchors' and terrain_render_revision, combined, when built
};

// Every cart with the bags strapped on it, and my bag set down or handled.
// `me` is my figure on foot (it reaches while I handle my bag), if drawn.
// `scripted_carts` are drawn and remembered like another player's: the
// benchmark's crowd (core/benchmark_driver.h); a real frame has none.
void add_carts_and_bags(render_data& data, const game_state& game, const prop_catalog& props, render_remote_avatar* me,
                        cart_fit_memo& fits, const std::vector<cart_in_world>& scripted_carts = {});

// `owner`'s bag (`bag_model`) or club being handled as `handling` has it:
// what moves, into data.handled_props, and `figure` (theirs) reaching for it.
void add_handling(render_data& data, render_remote_avatar& figure, const game_state& game, const bag_handling& handling,
                  std::uint64_t owner, const std::string& bag_model, const prop_catalog& props, cart_fit_memo& fits);

// The model of the bag `account` (0: me) has equipped.
std::string bag_model_of(const game_state& game, std::uint64_t account);
