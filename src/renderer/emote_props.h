#pragma once

// The emote props, a lit cigarette with its smoke and a beer can, placed in
// a holder's frame by their grip: mine in front of the camera, another
// player's at their figure's mouth (remote_avatar_batch.h), larger than life
// so they read from afar. The renderer draws the opaque pieces, then the
// smoke blended. GL-free.

#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

enum class emote_prop_mesh {
    cylinder,  // unit cylinder, y in [0, 1]
    sphere,    // unit sphere
    panel      // unit quad in XY
};

struct emote_prop {
    emote_prop_mesh mesh = emote_prop_mesh::sphere;
    glm::mat4 model{1.0f};
    glm::vec3 color{1.0f};
    float alpha = 1.0f;  // below 1: blended
};

// Where the props are held in the holder's frame (x right, y up, z
// forward; see camera_local.h), in world units: the cigarette from its
// filter to its lit tip, the can's centre and its turn (as
// camera_local_model takes it).
struct emote_grip {
    glm::vec3 cigarette_filter{0.0f};
    glm::vec3 cigarette_tip{0.0f};
    glm::vec3 can_centre{0.0f};
    glm::vec3 can_rotation{0.0f};
};

// Held in front of my camera, at the bottom of the view.
emote_grip camera_emote_grip();

// Who holds the props: the origin of their frame and a point it faces (only
// its yaw counts), how they hold them, how large the props are, and how far
// into each emote they are (none: not playing).
struct emote_holder {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f, 0.0f, 1.0f};
    emote_grip grip = camera_emote_grip();
    float scale = 1.0f;
    std::optional<float> smoke_elapsed;
    std::optional<float> drink_elapsed;
};

void append_emote_props(std::vector<emote_prop>& props, const emote_holder& holder);
