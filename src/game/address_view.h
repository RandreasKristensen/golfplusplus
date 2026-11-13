#pragma once

// Standing over my ball (game_mode::addressing): where I stand for a setup,
// where my eyes are and which way they look, the two framed looks R turns
// between, and my feet on the ground. One place
// for all of it, so others see me stand where I stand. Pure.

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

// R turning my head from one look to a framed one, eased in and out over
// address_snap_seconds.
struct look_snap {
    address_look from;
    address_look to;
    float elapsed = 0.0f;
};

// Whether `look` is nearer the down view than the target view: looking
// down at the ball, where the setup is pulled and the swing starts.
bool looking_down(const address_look& look, const camera_tuning& tuning);

// The framed look R turns to from `look`: the target view looking down,
// else the down view.
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

// A golfer stood to `setup`: me addressing my ball (game state and my
// figure), and others at theirs as they are shown, so the two agree.
struct address_stance {
    glm::vec3 position{0.0f};  // address_stance_position
    float facing = 0.0f;       // address_facing_yaw
};
address_stance stance_at_address(const play_area& area, const shot_setup& setup, const game_tuning& tuning,
                                 frame_profile* profile = nullptr);
// The world yaw `look` turns my head to.
float address_view_yaw(const shot_setup& setup, const address_look& look);

struct eye_view {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, 1.0f};  // unit
};

// My eyes over the stance, looking as `look` says.
eye_view address_eye(const play_area& area, const shot_setup& setup, const address_look& look,
                     const game_tuning& tuning);

// My shoes at address, on the ground either side of the stance.
struct address_feet {
    glm::vec3 ball{0.0f};
    glm::vec3 lead_foot{0.0f};
    glm::vec3 trail_foot{0.0f};
};

address_feet address_feet_for(const play_area& area, const shot_setup& setup, const game_tuning& tuning);
