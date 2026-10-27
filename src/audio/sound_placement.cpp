#include "audio/sound_placement.h"

#include "physics/vector_math.h"

#include <algorithm>

#include <glm/geometric.hpp>

sound_listener listener_looking(const glm::vec3& eye, const glm::vec3& target) {
    sound_listener listener;
    listener.position = eye;
    listener.forward = safe_normalize(target - eye, glm::vec3(0.0f, 0.0f, 1.0f));
    listener.left = yaw_left(listener.forward);
    return listener;
}

sound_mix place_sound(const sound_spread& spread, const glm::vec3& source, const sound_listener& listener,
                      const float meters_per_world_unit) {
    const glm::vec3 offset = source - listener.position;
    const float meters = glm::length(offset) * meters_per_world_unit;
    sound_mix mix;
    if (meters > spread.max) {
        mix.gain = 0.0f;
        return mix;
    }
    mix.gain = meters > spread.reference ? spread.reference / meters : 1.0f;
    const float length = glm::length(offset);
    mix.pan = length > 0.0001f ? std::clamp(-glm::dot(offset / length, listener.left), -1.0f, 1.0f) : 0.0f;
    return mix;
}

stereo_gains stereo_for(const float pan) {
    return stereo_gains{std::clamp(1.0f - pan, 0.0f, 1.0f), std::clamp(1.0f + pan, 0.0f, 1.0f)};
}
