#pragma once

// The emote props, a lit cigarette with its smoke and a beer can pouring
// its beer (their models in assets/world/props/), placed in a holder's frame by their grip:
// mine in front of the camera, another player's at their figure's mouth
// (remote_avatar_batch.h), larger than life so they read from afar. The
// renderer draws the opaque pieces, then the smoke and beer blended. GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_pieces.h"

#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

struct emote_prop {
    prop_mesh mesh = prop_mesh::sphere;
    glm::mat4 model{1.0f};
    glm::vec3 color{1.0f};
    float alpha = 1.0f;  // below 1: blended
    float glow = 0.0f;   // the least light it shows (lighting.glsl's flat_light)
};

// A can in the holder's frame: its centre, and where its top and its front
// (its plain side, away from its opening and label) point.
struct can_pose {
    glm::vec3 centre{0.0f};
    glm::vec3 top{0.0f, 1.0f, 0.0f};
    glm::vec3 front{0.0f, 0.0f, 1.0f};
};

// Where the props are held in the holder's frame (x left, y up, z forward;
// see camera_local.h), in world units: the cigarette from its filter to
// its lit tip; the can as it is lifted, and as it pours once tipped.
struct emote_grip {
    glm::vec3 cigarette_filter{0.0f};
    glm::vec3 cigarette_tip{0.0f};
    can_pose can_held;
    can_pose can_pouring;
};

// Held in front of my camera, at the bottom of the view: the cigarette left
// of the middle, the can right, tipped towards me to pour with its opening in view.
emote_grip camera_emote_grip();

// Who holds the props: their frame in the world (x left, y up, z forward
// from their eye: my camera's whole frame, so the props turn and tilt with
// my view; another's yaw-only), how they hold them, how large the props are,
// and how far into each emote they are (none: not playing). The smoke rises
// up the world from the tip and the beer falls down it from the can's
// opening, whichever way the frame tilts.
struct emote_holder {
    glm::mat4 frame{1.0f};
    emote_grip grip = camera_emote_grip();
    float scale = 1.0f;
    std::optional<float> smoke_elapsed;
    std::optional<float> drink_elapsed;
};

// Mine, held in my camera's whole frame: the same on screen wherever I look.
emote_holder camera_emote_holder(const glm::vec3& camera_position,
                                 const glm::vec3& camera_target,
                                 std::optional<float> smoke_elapsed,
                                 std::optional<float> drink_elapsed);

// The can's own frame in the world this far into the drink (its model's
// units scaled by the holder's).
glm::mat4 emote_can_frame(const emote_holder& holder, float drink_elapsed);

void append_emote_props(std::vector<emote_prop>& props, const emote_holder& holder, const prop_set& models,
                        const glow_floors& glow);
