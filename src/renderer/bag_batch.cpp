#include "renderer/bag_batch.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/cart_batch.h"
#include "renderer/swing_club.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
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
// Leave room under the grip and above the mouth for the club's head.
constexpr float bag_bottom_clearance = 0.04f;
constexpr float bag_head_clearance = 0.08f;

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

void append_carts_and_bags(prop_batch& batch, const render_data& data, const prop_catalog& props, const scene_colors& colors) {
    const glow_floors& glow = colors.glow;
    for (const render_cart& cart : data.carts) {
        const prop_model* cart_model = find_model(props, cart.model);
        if (cart_model == nullptr) {
            continue;
        }
        append_cart_model(batch, cart, *cart_model, colors);
        for (const auto& [mount, bag] : cart.bags) {
            const prop_model* bag_model = find_model(props, bag.model);
            if (bag_model != nullptr && cart_model->mounts.count(mount) != 0) {
                append_prop_model(batch, *bag_model, mounted_frame(cart_frame(cart), cart_model->mount(mount), *bag_model), glow,
                                  std::nullopt, bag_paint_color(colors.figures, bag.paint));
            }
        }
    }
    for (const render_ground_bag& bag : data.ground_bags) {
        if (const prop_model* model = find_model(props, bag.model)) {
            append_prop_model(batch, *model, ground_bag_frame(bag, *model), glow, std::nullopt, bag_paint_color(colors.figures, bag.paint));
        }
    }
    for (const render_handled_prop& prop : data.handled_props) {
        if (const prop_model* model = find_model(props, prop.model)) {
            const std::optional<glm::vec3> paint =
                prop.paint ? std::optional<glm::vec3>(bag_paint_color(colors.figures, *prop.paint)) : std::nullopt;
            if (model->kind == "club") {
                append_club_model(batch, *model, handled_prop_frame(prop), prop.club_loft_degrees, glow);
            } else {
                append_prop_model(batch, *model, handled_prop_frame(prop), glow, std::nullopt, paint);
            }
        }
    }
}

glm::mat4 club_in_bag_frame(const glm::mat4& bag_frame, const prop_model& bag, const prop_model& club) {
    // The model's y runs up the shaft from its head: head up is its y down the bag.
    const float depth = std::min(bag.point("mouth").y - bag.point("hang").y - bag_bottom_clearance,
                                 club.point("grip").y - club.point("face").y - bag_head_clearance);
    return bag_frame * glm::translate(glm::mat4(1.0f), bag.point("mouth") - world_up * depth) *
        glm::scale(glm::mat4(1.0f), glm::vec3(-1.0f, -1.0f, 1.0f)) *
        glm::translate(glm::mat4(1.0f), -club.point("grip"));
}

handled_club_pose handling_club_pose(const render_remote_avatar& figure, const float eye_height,
                                    const glm::mat4& bag_frame, const prop_model& bag, const prop_model& club,
                                    const float progress, const bool drawing) {
    const glm::vec3 grip = club.point("grip");
    const glm::vec3 head = club.point("face");
    const glm::mat4 seated = club_in_bag_frame(bag_frame, bag, club);
    const glm::vec3 mouth = glm::vec3(bag_frame * glm::vec4(bag.point("mouth"), 1.0f));
    const glm::vec3 up = glm::normalize(glm::vec3(bag_frame[1]));
    // Pull the grip completely clear before the shaft turns over the rim.
    glm::mat4 clear = seated;
    clear[3] += glm::vec4(up * (glm::dot(mouth - glm::vec3(seated * glm::vec4(grip, 1.0f)), up) + bag_head_clearance), 0.0f);
    const glm::mat4 carried = carried_club_frame(figure, eye_height, club);
    const float t = glm::clamp(progress, 0.0f, 1.0f);
    const bool turning = drawing ? t >= 0.5f : t < 0.5f;
    const float phase = t < 0.5f ? t * 2.0f : (t - 0.5f) * 2.0f;
    const float eased = phase * phase * (3.0f - 2.0f * phase);
    const glm::mat4 from = drawing ? (turning ? clear : seated) : (turning ? carried : clear);
    const glm::mat4 to = drawing ? (turning ? carried : clear) : (turning ? clear : seated);
    const glm::vec3 pivot = drawing && turning ? head : grip;
    const glm::mat4 at_pivot = glm::translate(glm::mat4(1.0f), pivot);
    const render_handled_prop moving{std::string(), std::nullopt, from * at_pivot, to * at_pivot, eased, 0.0f};
    const glm::mat4 frame = handled_prop_frame(moving) * glm::translate(glm::mat4(1.0f), -pivot);
    // Change holds while turning; the head is held above the rim throughout
    // insertion and withdrawal, so the hand never follows the grip into the bag.
    const glm::vec3 grasp = drawing ? glm::mix(head, grip, turning ? eased : 0.0f)
                                    : glm::mix(grip, head, turning ? eased : 1.0f);
    return handled_club_pose{frame, glm::vec3(frame * glm::vec4(grasp, 1.0f))};
}

glm::mat4 handled_prop_frame(const render_handled_prop& prop) {
    const float t = glm::clamp(prop.t, 0.0f, 1.0f);
    const glm::quat from = glm::quat_cast(glm::mat3(prop.from));
    const glm::quat to = glm::quat_cast(glm::mat3(prop.to));
    glm::mat4 frame = glm::mat4_cast(glm::slerp(from, to, t));
    const glm::vec3 at = glm::mix(glm::vec3(prop.from[3]), glm::vec3(prop.to[3]), t) +
        world_up * (prop.arc * std::sin(glm::pi<float>() * t));
    frame[3] = glm::vec4(at, 1.0f);
    return frame;
}

void append_figure_gear(prop_batch& batch, const render_remote_avatar& figure, const float eye_height, const prop_catalog& props,
                        const scene_colors& colors) {
    const glow_floors& glow = colors.glow;
    if (figure.dap) { append_dap_hand(batch, *figure.dap, colors.figures.skin, remote_tint(colors.figures, figure.player_id)); }
    if (const prop_model* bag = find_model(props, figure.bag_model)) {
        append_prop_model(batch, *bag, back_bag_frame(figure, eye_height, *bag), glow, std::nullopt, bag_paint_color(colors.figures, figure.bag_paint));
    }
    if (figure.swing || figure.seated) {
        return;
    }
    if (const prop_model* club = find_model(props, figure.carried_club_model)) {
        append_club_model(batch, *club, carried_club_frame(figure, eye_height, *club), figure.carried_club_loft_degrees, glow);
    }
}
