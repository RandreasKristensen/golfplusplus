#pragma once

// Standing over my ball (game_mode::addressing): where I stand for a setup,
// where my eyes are and which way they look, the two framed looks V turns
// between, and the setup as it shows on the ground at my feet. One place
// for all of it, so others see me stand where I stand. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/play_area.h"
#include "game/shot_setup.h"
#include "profiling/profiling.h"

#include <glm/vec3.hpp>

// Where my head looks, from facing the ball across the line: yaw positive
// towards the target in my own hand, pitch positive up. Radians.
struct address_look {
    float yaw = 0.0f;
    float pitch = 0.0f;
};

address_look target_look(const camera_tuning& tuning);
address_look down_look(const camera_tuning& tuning);
address_look clamped_look(const address_look& look, const camera_tuning& tuning);

// V turning my head from one look to a framed one, eased in and out over
// address_snap_seconds.
struct look_snap {
    address_look from;
    address_look to;
    float elapsed = 0.0f;
};

// The framed look V turns to from `look`: the down view from anywhere
// nearer the target view, else the target view.
address_look snap_destination(const address_look& look, const camera_tuning& tuning);
address_look snap_look_at(const look_snap& snap, const camera_tuning& tuning);
bool snap_done(const look_snap& snap, const camera_tuning& tuning);

// The middle of my stance, on the ground: to the ball's side by my hand
// (the lies' stance distance), moved back along the aim as the ball is
// played forward (setup.ball_position_meters a unit).
glm::vec3 address_stance_position(const play_area& area, const shot_setup& setup, const game_tuning& tuning,
                                  frame_profile* profile = nullptr);

// The way I face at address: across the line, at the ball (world yaw).
float address_facing_yaw(const shot_setup& setup);
// The world yaw `look` turns my head to.
float address_view_yaw(const shot_setup& setup, const address_look& look);

struct eye_view {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};  // unit
};

// My eyes over the stance, looking as `look` says.
eye_view address_eye(const play_area& area, const shot_setup& setup, const address_look& look,
                     const game_tuning& tuning);

// The setup as it shows on the ground at address.
struct address_marks {
    glm::vec3 ball{0.0f};
    // The clubface's leading edge soled behind the ball, past the head
    // both ways: it turns with the face.
    glm::vec3 face_heel{0.0f};
    glm::vec3 face_toe{0.0f};
    // An alignment stick along my toes, parallel to the aim (the path).
    glm::vec3 stick_back{0.0f};
    glm::vec3 stick_front{0.0f};
    // On the stick, across from where the club's stock setup puts the ball.
    glm::vec3 stock_tick{0.0f};
    glm::vec3 stance{0.0f};  // address_stance_position
    // My feet either side of it along the aim (the lies' stance width).
    glm::vec3 lead_foot{0.0f};
    glm::vec3 trail_foot{0.0f};
};

address_marks address_marks_for(const play_area& area, const shot_setup& setup, const club_definition& club,
                                const game_tuning& tuning);
