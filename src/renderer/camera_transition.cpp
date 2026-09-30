#include "renderer/camera_transition.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {

float smoothstep01(const float t) {
    const float x = std::clamp(t, 0.0f, 1.0f);
    return x * x * (3.0f - 2.0f * x);
}

float mix(const float a, const float b, const float t) {
    return a + (b - a) * t;
}

glm::vec3 mix(const glm::vec3& a, const glm::vec3& b, const float t) {
    return a + (b - a) * t;
}

} // namespace

camera_view blend_camera_views(const camera_view& from, const camera_view& to, const float t) {
    const float w = std::clamp(t, 0.0f, 1.0f);
    const glm::vec3 from_look = from.target - from.position;
    const glm::vec3 to_look = to.target - to.position;
    const float from_distance = glm::length(from_look);
    const float to_distance = glm::length(to_look);
    if (from_distance <= 0.0001f || to_distance <= 0.0001f) {
        return w < 1.0f ? from : to;
    }

    glm::vec3 direction = mix(from_look / from_distance, to_look / to_distance, w);
    const float direction_length = glm::length(direction);
    // Opposite directions have no defined halfway turn; swap view at the midpoint.
    direction = direction_length > 0.0001f
        ? direction / direction_length
        : (w < 0.5f ? from_look / from_distance : to_look / to_distance);

    camera_view view;
    view.position = mix(from.position, to.position, w);
    view.target = view.position + direction * mix(from_distance, to_distance, w);
    view.fov_degrees = mix(from.fov_degrees, to.fov_degrees, w);
    return view;
}

camera_transition_state snap_camera_transition(const camera_rig rig, const camera_view& desired) {
    camera_transition_state state;
    state.initialized = true;
    state.rig = rig;
    state.previous_desired = desired;
    state.from = desired;
    state.shown = desired;
    return state;
}

camera_transition_state update_camera_transition(const camera_transition_state& state,
                                                 const camera_rig rig,
                                                 const camera_view& desired,
                                                 const float dt,
                                                 const camera_transition_settings& settings) {
    if (!state.initialized) {
        return snap_camera_transition(rig, desired);
    }

    camera_transition_state next = state;
    const bool rig_changed = rig != state.rig;
    const bool teleported = glm::length(desired.position - state.previous_desired.position) > settings.jump_distance;
    if ((rig_changed || teleported) && settings.duration_seconds > 0.0f) {
        next.from = state.shown;
        next.elapsed = 0.0f;
        next.duration = settings.duration_seconds;
    } else {
        next.elapsed = state.elapsed + std::max(0.0f, dt);
    }
    next.rig = rig;
    next.previous_desired = desired;

    if (next.duration <= 0.0f || next.elapsed >= next.duration) {
        next.duration = 0.0f;
        next.shown = desired;
        return next;
    }

    next.shown = blend_camera_views(next.from, desired, smoothstep01(next.elapsed / next.duration));
    return next;
}
