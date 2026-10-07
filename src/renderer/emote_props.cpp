#include "renderer/emote_props.h"

#include "renderer/camera_local.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr int smoke_puff_count = 5;

const glm::vec3 cigarette_color(0.88f, 0.82f, 0.62f);
const glm::vec3 smoke_color(0.78f, 0.80f, 0.78f);
const glm::vec3 can_color(0.76f, 0.76f, 0.70f);
const glm::vec3 label_color(0.78f, 0.18f, 0.12f);
const glm::vec3 lid_color(0.18f, 0.18f, 0.16f);

void append_cigarette(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed) {
    const glm::vec3& eye = holder.eye;
    const glm::vec3& target = holder.target;
    const float s = holder.scale;
    const float t = std::max(0.0f, elapsed);
    const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
    const glm::vec3& tip = holder.grip.cigarette_tip;
    props.push_back(emote_prop{emote_prop_mesh::cylinder,
                               local_segment_model(eye, target, holder.grip.cigarette_filter, tip, 0.026f * s), cigarette_color,
                               1.0f});
    props.push_back(emote_prop{emote_prop_mesh::sphere, local_sphere_model(eye, target, tip, 0.046f * s),
                               glm::vec3(0.95f, 0.20f + ember * 0.20f, 0.10f), 1.0f});

    for (int i = 0; i < smoke_puff_count; ++i) {
        const float f = static_cast<float>(i);
        const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
        const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
        const glm::vec3 puff = tip + glm::vec3(sway + f * 0.012f, 0.10f + rise, -rise * 0.10f) * s;
        props.push_back(emote_prop{emote_prop_mesh::sphere, local_sphere_model(eye, target, puff, (0.065f + rise * 0.075f) * s),
                                   smoke_color, std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f)});
    }
}

void append_can(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed) {
    const float s = holder.scale;
    const float bob = std::sin(elapsed * 9.0f) * 0.035f;
    // The can's own frame: its label and lid tilt with it.
    const glm::mat4 can = camera_local_model(holder.eye, holder.target, holder.grip.can_centre + glm::vec3(0.0f, bob * s, 0.0f),
                                             holder.grip.can_rotation, glm::vec3(1.0f));
    const glm::mat4 body = glm::translate(glm::scale(can, glm::vec3(0.145f, 0.48f, 0.145f) * s), glm::vec3(0.0f, -0.5f, 0.0f));
    props.push_back(emote_prop{emote_prop_mesh::cylinder, body, can_color, 1.0f});
    const glm::mat4 label =
        glm::scale(glm::translate(can, glm::vec3(0.0f, 0.0f, -0.155f) * s), glm::vec3(0.118f * s, 0.108f * s, 1.0f));
    props.push_back(emote_prop{emote_prop_mesh::panel, label, label_color, 1.0f});
    const glm::mat4 lid = glm::scale(glm::rotate(glm::translate(can, glm::vec3(0.0f, 0.25f, -0.03f) * s), glm::radians(90.0f),
                                                 glm::vec3(1.0f, 0.0f, 0.0f)),
                                     glm::vec3(0.052f * s, 0.024f * s, 1.0f));
    props.push_back(emote_prop{emote_prop_mesh::panel, lid, lid_color, 1.0f});
}
}

emote_grip camera_emote_grip() {
    emote_grip grip;
    grip.cigarette_filter = glm::vec3(0.08f, -0.42f, 0.78f);
    grip.cigarette_tip = glm::vec3(0.30f, -0.47f, 1.12f);
    grip.can_centre = glm::vec3(-0.24f, -0.50f, 0.82f);
    grip.can_rotation = glm::vec3(glm::radians(-8.0f), 0.0f, glm::radians(10.0f));
    return grip;
}

void append_emote_props(std::vector<emote_prop>& props, const emote_holder& holder) {
    if (holder.smoke_elapsed) {
        append_cigarette(props, holder, *holder.smoke_elapsed);
    }
    if (holder.drink_elapsed) {
        append_can(props, holder, *holder.drink_elapsed);
    }
}
