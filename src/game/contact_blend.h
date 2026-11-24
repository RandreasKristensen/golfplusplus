#pragma once

// A ball in flight that a contact changes (game/ball_contact.h) is shown
// moving from where it was drawn to its new path over
// net.shot_correction_seconds: the offset between the two, fading to zero.
// Presentation only. Pure.

#include <algorithm>

#include <glm/vec3.hpp>

struct contact_blend {
    glm::vec3 offset{0.0f};  // drawn minus the new path, when the contact arrived
    float elapsed = 0.0f;
    float seconds = 0.0f;  // how long it takes
};

// The offset still to add to the new path.
inline glm::vec3 blend_offset(const contact_blend& blend) {
    const float t = blend.seconds > 0.0f ? std::min(1.0f, blend.elapsed / blend.seconds) : 1.0f;
    return blend.offset * (1.0f - t);
}
