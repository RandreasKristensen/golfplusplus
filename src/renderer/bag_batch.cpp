#include "renderer/bag_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Set down, a bag leans back on its stand this far from upright; slung on
// a back it crosses it at this angle, its bottom at the hips.
constexpr float stand_lean_degrees = 14.0f;
constexpr float back_tilt_degrees = 22.0f;
// Its bottom against the hips, as shares of the figure's eye height: this
// high, this far behind the figure.
constexpr float back_bottom_height_share = 0.3f;
constexpr float back_out_share = 0.11f;
// A carried club: held at this share of the eye height, this far out to
// the golfer's side, its shaft this far from upright, head forward.
constexpr float carry_hand_height_share = 0.5f;
constexpr float carry_side_meters = 0.26f;
constexpr float carry_lean_degrees = 46.0f;

const prop_model* find_model(const prop_catalog& props, const std::string& id) {
    const auto found = props.find(id);
    return found != props.end() ? &found->second : nullptr;
}
}

glm::mat4 ground_bag_frame(const render_ground_bag& bag, const prop_model& model) {
    const glm::vec3 front = yaw_direction(bag.yaw);
    const float lean = glm::radians(stand_lean_degrees);
    const glm::vec3 up = world_up * std::cos(lean) - front * std::sin(lean);
    return posed_model(bag.position, up, front, glm::vec3(1.0f)) * glm::translate(glm::mat4(1.0f), -model.point("hang"));
}

glm::mat4 back_bag_frame(const render_remote_avatar& figure, const float eye_height, const prop_model& model) {
    const glm::vec3 forward = yaw_direction(figure.yaw);
    const glm::vec3 left = yaw_left(forward);
    const float tilt = glm::radians(back_tilt_degrees);
    const glm::vec3 up = world_up * std::cos(tilt) + left * std::sin(tilt);
    const glm::vec3 bottom =
        figure.position + world_up * (eye_height * back_bottom_height_share) - forward * (eye_height * back_out_share);
    // Its pocket faces out, away from the back.
    return posed_model(bottom, up, -forward, glm::vec3(1.0f)) * glm::translate(glm::mat4(1.0f), -model.point("hang"));
}

glm::mat4 carried_club_frame(const render_remote_avatar& figure, const float eye_height, const prop_model& model) {
    const glm::vec3 forward = yaw_direction(figure.yaw);
    const glm::vec3 side = -yaw_left(forward) * (figure.left_handed ? -1.0f : 1.0f);
    const float lean = glm::radians(carry_lean_degrees);
    // The model's y runs up the shaft from the head; its z is where the face looks.
    const glm::vec3 up = world_up * std::cos(lean) - forward * std::sin(lean);
    const glm::vec3 grip = figure.position + world_up * (eye_height * carry_hand_height_share) + side * carry_side_meters;
    return posed_model(grip, up, forward, glm::vec3(1.0f)) * glm::translate(glm::mat4(1.0f), -model.point("grip"));
}

void append_carts_and_bags(prop_batch& batch, const render_data& data, const prop_catalog& props, const glow_floors& glow) {
    for (const render_cart& cart : data.carts) {
        const prop_model* cart_model = find_model(props, cart.model);
        if (cart_model == nullptr) {
            continue;
        }
        append_cart_model(batch, cart, *cart_model, glow);
        for (const auto& [mount, bag] : cart.bags) {
            const prop_model* bag_model = find_model(props, bag);
            if (bag_model != nullptr && cart_model->mounts.count(mount) != 0) {
                append_prop_model(batch, *bag_model, mounted_frame(cart_frame(cart), cart_model->mount(mount), *bag_model), glow);
            }
        }
    }
    for (const render_ground_bag& bag : data.ground_bags) {
        if (const prop_model* model = find_model(props, bag.model)) {
            append_prop_model(batch, *model, ground_bag_frame(bag, *model), glow);
        }
    }
}

void append_figure_gear(prop_batch& batch, const render_remote_avatar& figure, const float eye_height, const prop_catalog& props,
                        const glow_floors& glow) {
    if (const prop_model* bag = find_model(props, figure.bag_model)) {
        append_prop_model(batch, *bag, back_bag_frame(figure, eye_height, *bag), glow);
    }
    if (figure.swing || figure.seated) {
        return;
    }
    if (const prop_model* club = find_model(props, figure.carried_club_model)) {
        append_prop_model(batch, *club, carried_club_frame(figure, eye_height, *club), glow);
    }
}
