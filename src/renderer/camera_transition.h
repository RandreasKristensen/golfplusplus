#pragma once

#include <glm/vec3.hpp>

// GL-free camera blending (unit tested without an OpenGL context).
//
// The camera for each game mode (its "rig") is recomputed from game state
// every frame. Switching rigs, or a rig teleporting (re-tee, next hole), would
// otherwise cut straight to the new view. This eases from the view that was on
// screen to the live view of the new rig over a short fixed time, so the
// camera never jumps. It is presentation state only: never saved, never read
// by game logic.

struct camera_view {
    glm::vec3 position{0.0f};
    glm::vec3 target{0.0f, 0.0f, 1.0f};
    float fov_degrees = 0.0f;
};

enum class camera_rig {
    walking,  // on foot and in the cart
    aiming,
    addressing,
    following_shot
};

// From game_tuning::camera.
struct camera_transition_settings {
    float duration_seconds = 0.0f;  // length of one blend
    // A rig whose eye moves further than this in one frame has teleported,
    // and is blended to like a rig change.
    float jump_distance = 0.0f;
};

struct camera_transition_state {
    bool initialized = false;
    camera_rig rig = camera_rig::walking;
    // Last frame's live rig view, for teleport detection.
    camera_view previous_desired;
    // What was on screen when the current blend started.
    camera_view from;
    // What was on screen last frame.
    camera_view shown;
    float elapsed = 0.0f;
    float duration = 0.0f;
};

// Weight `t` in [0, 1] from `from` to `to`. The eye moves in a straight line;
// the look direction and look distance are blended separately so the view
// turns smoothly instead of the target sliding through the scene.
camera_view blend_camera_views(const camera_view& from, const camera_view& to, float t);

// Starts following `desired` with no blend (first frame, menus, course start).
camera_transition_state snap_camera_transition(camera_rig rig, const camera_view& desired);

// Advances the blend by `dt` seconds towards this frame's live rig view and
// returns the new state; the view to draw is the result's `shown`.
camera_transition_state update_camera_transition(const camera_transition_state& state,
                                                 camera_rig rig,
                                                 const camera_view& desired,
                                                 float dt,
                                                 const camera_transition_settings& settings);
