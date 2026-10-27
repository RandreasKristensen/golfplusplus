#pragma once

// What a player holds while using an item (game/item_catalog.h): what they
// smoke, lit with its smoke rising, and what they drink, pouring (their
// models in assets/world/props/), placed in a holder's frame by their grip:
// mine in front of the camera, another player's at their figure's mouth
// (remote_avatar_batch.h), larger than life so they read from afar. Drawn
// into the prop batch (prop_batch.h), the smoke and beer blended. GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"

#include <optional>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// A can in the holder's frame: its centre, and where its top and its front
// (its plain side, away from its opening and label) point.
struct can_pose {
    glm::vec3 centre{0.0f};
    glm::vec3 top{0.0f, 1.0f, 0.0f};
    glm::vec3 front{0.0f, 0.0f, 1.0f};
};

// Where the props are held in the holder's frame (x left, y up, z forward;
// see camera_local.h), in world units: what is smoked from its filter to
// its lit tip; the can as it is lifted, and as it pours once tipped.
struct held_grip {
    glm::vec3 smokable_filter{0.0f};
    glm::vec3 smokable_tip{0.0f};
    can_pose can_held;
    can_pose can_pouring;
};

// Held in front of my camera, at the bottom of the view: the cigarette left
// of the middle, the can right, tipped towards me to pour with its opening in view.
held_grip camera_held_grip();

// A use in progress as the frame carries it: its item's model by id, and
// how far into it.
struct render_item_use {
    std::string model;
    float elapsed = 0.0f;
};

// A use in progress: its item's model (of kind cigarette for a smokable,
// can for a drinkable) and how far into it.
struct held_use {
    const prop_model* model = nullptr;
    float elapsed = 0.0f;
};

// Who holds the props: their frame in the world (x left, y up, z forward
// from their eye: my camera's whole frame, so the props turn and tilt with
// my view; another's yaw-only), how they hold them, how large the props are,
// and what they smoke and drink (none: not using it). The smoke rises up
// the world from the tip and the beer falls down it from the can's opening,
// whichever way the frame tilts.
struct prop_holder {
    glm::mat4 frame{1.0f};
    held_grip grip = camera_held_grip();
    float scale = 1.0f;
    std::optional<held_use> smokable;
    std::optional<held_use> drinkable;
};

// Mine, held in my camera's whole frame: the same on screen wherever I look.
prop_holder camera_prop_holder(const glm::vec3& camera_position,
                               const glm::vec3& camera_target,
                               std::optional<held_use> smokable,
                               std::optional<held_use> drinkable);

// The can's own frame in the world this far into the drink (its model's
// units scaled by the holder's).
glm::mat4 held_can_frame(const prop_holder& holder, float drink_elapsed);

void append_held_props(prop_batch& batch, const prop_holder& holder, const glow_floors& glow);
