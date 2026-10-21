#include "renderer/emote_props.h"

#include "game/lighting_fields.h"
#include "physics/vector_math.h"
#include "renderer/camera_local.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr int smoke_puff_count = 5;
constexpr int beer_drop_count = 9;
// Seconds the can takes to tip from held to pouring; the beer runs once it has.
constexpr float pour_tip_seconds = 0.35f;
constexpr float no_glow = 0.0f;

const glm::vec3 frame_ahead(0.0f, 0.0f, 1.0f);

glm::mat4 sphere_at(const glm::vec3& centre, const float radius) {
    return glm::scale(glm::translate(glm::mat4(1.0f), centre), glm::vec3(radius));
}

// The pieces of `model` in `frame`; an "ember" part takes `ember_color`.
void append_model(std::vector<emote_prop>& props, const prop_model& model, const glm::mat4& frame, const glow_floors& glow,
                  const glm::vec3& ember_color) {
    for (const prop_piece& piece : prop_pieces(model, frame)) {
        float floor = no_glow;
        for (const share_field<glow_floors>& field : glow_fields) {
            if (piece.part->glow == field.key) {
                floor = glow.*field.member;
            }
        }
        props.push_back(emote_prop{piece.mesh, piece.model, piece.part->tag == "ember" ? ember_color : piece.color, 1.0f, floor});
    }
}

// The cigarette's own frame, its model's z from 0 at the filter to 1 at the
// tip: stretched evenly from `filter` to `tip`.
glm::mat4 cigarette_frame(const glm::vec3& filter, const glm::vec3& tip) {
    const glm::vec3 along = tip - filter;
    const float length = glm::length(along);
    const glm::vec3 direction = safe_normalize(along, frame_ahead);
    const glm::vec3 reference = std::abs(direction.y) > 0.95f ? frame_ahead : world_up;
    const glm::vec3 x = glm::normalize(glm::cross(reference, direction));
    const glm::vec3 y = glm::cross(direction, x);
    glm::mat4 frame(1.0f);
    frame[0] = glm::vec4(x * length, 0.0f);
    frame[1] = glm::vec4(y * length, 0.0f);
    frame[2] = glm::vec4(along, 0.0f);
    frame[3] = glm::vec4(filter, 1.0f);
    return frame;
}

void append_cigarette(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed,
                      const prop_model& cigarette, const glow_floors& glow) {
    const glm::mat4& frame = holder.frame;
    const float s = holder.scale;
    const float t = std::max(0.0f, elapsed);
    const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
    const glm::mat4 own = frame * cigarette_frame(holder.grip.cigarette_filter, holder.grip.cigarette_tip);
    append_model(props, cigarette, own, glow,
                 glm::mix(cigarette.color("ember_dim"), cigarette.color("ember_bright"), ember));

    const glm::vec3 tip_world(own * glm::vec4(cigarette.point("smoke"), 1.0f));
    const glm::vec3 right(frame[0]);
    const glm::vec3 ahead = safe_normalize(horizontal(glm::vec3(frame[2])), glm::vec3(0.0f, 0.0f, 1.0f));
    for (int i = 0; i < smoke_puff_count; ++i) {
        const float f = static_cast<float>(i);
        const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
        const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
        const glm::vec3 puff =
            tip_world + (right * (sway + f * 0.012f) + world_up * (0.10f + rise) - ahead * (rise * 0.10f)) * s;
        props.push_back(emote_prop{prop_mesh::sphere, sphere_at(puff, (0.065f + rise * 0.075f) * s), cigarette.color("smoke"),
                                   std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f), no_glow});
    }
}

void append_can(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed, const prop_model& can,
                const glow_floors& glow) {
    const float s = holder.scale;
    const glm::mat4 frame = emote_can_frame(holder, elapsed);
    append_model(props, can, frame, glow, glm::vec3(0.0f));
    const float t = std::max(0.0f, elapsed);
    if (t < pour_tip_seconds) {
        return;
    }

    // The beer runs over the lip on the opening's side and falls down the
    // world, whichever way the frame tilts.
    const glm::vec3 lip(frame * glm::vec4(can.point("pour"), 1.0f));
    const glm::vec3 out = safe_normalize(horizontal(-glm::vec3(frame[2])), -glm::vec3(holder.frame[2]));
    const float run = t - pour_tip_seconds;
    for (int i = 0; i < beer_drop_count; ++i) {
        const float f = static_cast<float>(i);
        const float fall = std::fmod(run * 1.6f + f / static_cast<float>(beer_drop_count), 1.0f);
        const float wobble = std::sin(t * 13.0f + f * 2.3f) * 0.012f;
        const glm::vec3 drop = lip + (out * (fall * 0.16f + wobble) - world_up * (fall * fall * 0.70f)) * s;
        const bool foam = i % 4 == 0;
        props.push_back(emote_prop{prop_mesh::sphere, sphere_at(drop, (0.042f - fall * 0.016f) * s),
                                   can.color(foam ? "foam" : "beer"), std::clamp(0.88f - fall * 0.5f, 0.3f, 0.88f),
                                   no_glow});
    }
}
}

glm::mat4 emote_can_frame(const emote_holder& holder, const float drink_elapsed) {
    const float s = holder.scale;
    const float t = std::max(0.0f, drink_elapsed);
    const float tipped = glm::smoothstep(0.0f, pour_tip_seconds, t);
    const can_pose& held = holder.grip.can_held;
    const can_pose& pouring = holder.grip.can_pouring;
    const float bob = std::sin(t * 9.0f) * 0.035f;
    const glm::vec3 centre = glm::mix(held.centre, pouring.centre, tipped) + glm::vec3(0.0f, bob * s, 0.0f);
    return holder.frame * posed_model(centre, glm::mix(held.top, pouring.top, tipped),
                                      glm::mix(held.front, pouring.front, tipped), glm::vec3(s));
}

emote_grip camera_emote_grip() {
    emote_grip grip;
    grip.cigarette_filter = glm::vec3(0.08f, -0.42f, 0.78f);
    grip.cigarette_tip = glm::vec3(0.30f, -0.47f, 1.12f);
    grip.can_held = can_pose{glm::vec3(-0.24f, -0.50f, 0.82f), glm::vec3(-0.174f, 0.975f, -0.137f), glm::vec3(0.0f, 0.139f, 0.990f)};
    grip.can_pouring = can_pose{glm::vec3(-0.22f, -0.32f, 0.86f), glm::vec3(0.15f, 0.62f, -0.77f), glm::vec3(0.0f, 0.78f, 0.62f)};
    return grip;
}

emote_holder camera_emote_holder(const glm::vec3& camera_position,
                                 const glm::vec3& camera_target,
                                 const std::optional<float> smoke_elapsed,
                                 const std::optional<float> drink_elapsed) {
    emote_holder holder;
    holder.frame = camera_view_frame(camera_position, camera_target);
    holder.smoke_elapsed = smoke_elapsed;
    holder.drink_elapsed = drink_elapsed;
    return holder;
}

void append_emote_props(std::vector<emote_prop>& props, const emote_holder& holder, const prop_set& models,
                        const glow_floors& glow) {
    if (holder.smoke_elapsed) {
        append_cigarette(props, holder, *holder.smoke_elapsed, models.cigarette, glow);
    }
    if (holder.drink_elapsed) {
        append_can(props, holder, *holder.drink_elapsed, models.beer_can, glow);
    }
}
