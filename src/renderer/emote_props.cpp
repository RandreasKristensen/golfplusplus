#include "renderer/emote_props.h"

#include "physics/vector_math.h"
#include "renderer/camera_local.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
constexpr int smoke_puff_count = 5;
constexpr float no_glow = 0.0f;

// In the holder's frame, the yaw-only camera_local helpers place props at
// its origin facing +z; the frame then carries them into the world.
const glm::vec3 frame_origin(0.0f);
const glm::vec3 frame_ahead(0.0f, 0.0f, 1.0f);

glm::mat4 sphere_at(const glm::vec3& centre, const float radius) {
    return glm::scale(glm::translate(glm::mat4(1.0f), centre), glm::vec3(radius));
}

void append_cigarette(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed,
                      const emote_colors& colors, const float ember_glow) {
    const glm::mat4& frame = holder.frame;
    const float s = holder.scale;
    const float t = std::max(0.0f, elapsed);
    const float ember = 0.55f + 0.45f * std::sin(t * 24.0f);
    const glm::vec3& tip = holder.grip.cigarette_tip;
    props.push_back(emote_prop{emote_prop_mesh::cylinder,
                               frame * local_segment_model(frame_origin, frame_ahead, holder.grip.cigarette_filter, tip, 0.026f * s),
                               colors.cigarette, 1.0f, no_glow});
    const glm::vec3 tip_world(frame * glm::vec4(tip, 1.0f));
    props.push_back(emote_prop{emote_prop_mesh::sphere, frame * sphere_at(tip, 0.046f * s),
                               glm::mix(colors.ember_dim, colors.ember_bright, ember), 1.0f, ember_glow});

    const glm::vec3 right(frame[0]);
    const glm::vec3 ahead = safe_normalize(horizontal(glm::vec3(frame[2])), glm::vec3(0.0f, 0.0f, 1.0f));
    for (int i = 0; i < smoke_puff_count; ++i) {
        const float f = static_cast<float>(i);
        const float rise = std::fmod(t * 0.55f + f * 0.18f, 0.90f);
        const float sway = std::sin(t * 4.0f + f * 1.7f) * 0.055f;
        const glm::vec3 puff =
            tip_world + (right * (sway + f * 0.012f) + world_up * (0.10f + rise) - ahead * (rise * 0.10f)) * s;
        props.push_back(emote_prop{emote_prop_mesh::sphere, sphere_at(puff, (0.065f + rise * 0.075f) * s), colors.smoke,
                                   std::clamp(0.48f - rise * 0.42f, 0.04f, 0.42f), no_glow});
    }
}

void append_can(std::vector<emote_prop>& props, const emote_holder& holder, const float elapsed, const emote_colors& colors) {
    const float s = holder.scale;
    const float bob = std::sin(elapsed * 9.0f) * 0.035f;
    // The can's own frame: its label and lid tilt with it.
    const glm::mat4 can = holder.frame * camera_local_model(frame_origin, frame_ahead,
                                                            holder.grip.can_centre + glm::vec3(0.0f, bob * s, 0.0f),
                                                            holder.grip.can_rotation, glm::vec3(1.0f));
    const glm::mat4 body = glm::translate(glm::scale(can, glm::vec3(0.145f, 0.48f, 0.145f) * s), glm::vec3(0.0f, -0.5f, 0.0f));
    props.push_back(emote_prop{emote_prop_mesh::cylinder, body, colors.can, 1.0f, no_glow});
    const glm::mat4 label =
        glm::scale(glm::translate(can, glm::vec3(0.0f, 0.0f, -0.155f) * s), glm::vec3(0.118f * s, 0.108f * s, 1.0f));
    props.push_back(emote_prop{emote_prop_mesh::panel, label, colors.can_label, 1.0f, no_glow});
    const glm::mat4 lid = glm::scale(glm::rotate(glm::translate(can, glm::vec3(0.0f, 0.25f, -0.03f) * s), glm::radians(90.0f),
                                                 glm::vec3(1.0f, 0.0f, 0.0f)),
                                     glm::vec3(0.052f * s, 0.024f * s, 1.0f));
    props.push_back(emote_prop{emote_prop_mesh::panel, lid, colors.can_lid, 1.0f, no_glow});
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

void append_emote_props(std::vector<emote_prop>& props, const emote_holder& holder, const scene_colors& colors) {
    if (holder.smoke_elapsed) {
        append_cigarette(props, holder, *holder.smoke_elapsed, colors.emotes, colors.glow.ember);
    }
    if (holder.drink_elapsed) {
        append_can(props, holder, *holder.drink_elapsed, colors.emotes);
    }
}
