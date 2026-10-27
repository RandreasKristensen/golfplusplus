#pragma once

// Where a sound in the world is heard from: its gain by inverse distance
// (about 6 dB quieter for each doubling) between its reference and maximum
// distances, silent past the maximum, and its pan by its direction from the
// listener, who hears from where the camera sees. Pure.

#include <glm/vec3.hpp>

// How a sound carries (the manifest's reference_distance and max_distance),
// in metres: full level up to `reference`, silent past `max`.
struct sound_spread {
    float reference = 0.0f;
    float max = 0.0f;
};

struct sound_listener {
    glm::vec3 position{0.0f};
    glm::vec3 forward{0.0f, 0.0f, 1.0f};
    glm::vec3 left{1.0f, 0.0f, 0.0f};
};

// A placed sound's level (0..1, on top of its mix) and pan (-1 on the
// listener's left, 1 on the right).
struct sound_mix {
    float gain = 1.0f;
    float pan = 0.0f;
};

// The listener at `eye` looking at `target`.
sound_listener listener_looking(const glm::vec3& eye, const glm::vec3& target);

// A sound at `source` (world units) as `listener` hears it.
sound_mix place_sound(const sound_spread& spread, const glm::vec3& source, const sound_listener& listener,
                      float meters_per_world_unit);

// The share of full level each side plays a sound panned by `pan`: the
// side it is on at full level, the other fading to nothing at a hard pan.
struct stereo_gains {
    float left = 1.0f;
    float right = 1.0f;
};
stereo_gains stereo_for(float pan);
