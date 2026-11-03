#pragma once

// The club at a ball, mine and other players' alike: its model (a `club`
// model in assets/world/props/) soled just behind the ball at address, its
// face turned about the shaft as the setup opens or closes it and its
// shaft leaning towards the target as the ball is played back, toe away
// from the golfer (mirrored for a left-hander); swung back by the arms
// and the wrists' hinge as far as the swing has raised it, a putt by the
// shoulders' short pendulum with quiet wrists. Drawn into the
// prop batch (prop_batch.h). GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_batch.h"

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Where a club model's head is from its grip, from its points: the face's
// middle down the shaft, out from the shaft's line and ahead of it, and the
// sole's heel end, which rests on the ground at address.
struct club_fit {
    float shaft_length = 0.0f;
    float face_from_shaft = 0.0f;
    float face_ahead = 0.0f;
    glm::vec3 sole{0.0f};  // from the grip, in the model's frame
};

club_fit club_fit_of(const prop_model& club);

struct render_club_pose {
    glm::vec3 ball_position{0.0f};  // the drawn ball's centre, its bottom on the ground
    float aim_angle = 0.0f;         // the path (world yaw)
    float face_turn = 0.0f;         // from square to the path, radians, positive left as yaw
    float shaft_lean = 0.0f;        // radians, the grip towards the target
    float raise = 0.0f;             // how far it is swung back (game/swing.h swing_club_raise)
    bool left_handed = false;
    bool putt = false;
    std::string club_model;
    club_fit fit;  // club_model's
};

// Where the golfer's hands hold the club now.
glm::vec3 swing_club_grip(const render_club_pose& pose, float ball_visual_radius_meters);

// The club model's own frame in the world: its grip point in the hands,
// its y up the shaft from the head, its z where the face looks.
glm::mat4 swing_club_frame(const render_club_pose& pose, float ball_visual_radius_meters, const prop_model& club);

void append_swing_club(prop_batch& batch, const render_club_pose& pose, float ball_visual_radius_meters,
                       const prop_model& club, const glow_floors& glow);
