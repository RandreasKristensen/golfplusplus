#pragma once

// What a swing does to the club at impact (physics/impact.h's delivery),
// from the club and how it is set up (game/shot_setup.h): where the stance
// aims (the path), how far the face is opened, where the ball sits in the
// stance and how far the club is swung back; and what the ball lies on
// changes of it. Pure.

#include "game/club_definition.h"
#include "game/game_tuning.h"
#include "game/shot_setup.h"
#include "physics/impact.h"

// The club's speed share at `backswing` (0 none .. 1 full .. past 1
// overswing): a full swing's clock, a putter's pendulum.
float backswing_speed_share(const club_definition& club, float backswing, const swing_tuning& tuning);

// The face's loft gained by opening it `face_open` radians about the club's
// shaft, which leans `lie_degrees` from level: an open face lies back.
float face_open_loft_gain(float face_open, float lie_degrees);

// `club` swung as `setup` says: along its aim, the face where
// world_face_yaw points it and lying back as far as it is opened, the ball
// back in the stance leaning the shaft forward (less loft) and steepening
// the attack, forward the opposite. Struck in the face's middle.
delivery club_delivery(const club_definition& club, const shot_setup& setup, const swing_tuning& tuning);

// The ground a stance stands on, in radians: how far the ball sits above
// the feet (below: negative), and how far the stance rises towards where
// it aims (downhill: negative).
struct stance_slope {
    float ball_above_feet = 0.0f;
    float uphill = 0.0f;
};

// What a ball is struck from: its lie, the stance's slope, which side of
// it the golfer stands (their hand) and how wet the course is (0..1).
struct ball_on_lie {
    ball_lie lie = ball_lie::fairway;
    stance_slope stance;
    float wetness = 0.0f;
    handedness hand = handedness::right;
};

// `swing` played from `on`, swung back `backswing`. The stance tilts the
// swing with the slope (uphill adds loft and lifts the attack), and a ball
// above the feet turns the lofted face towards the golfer's lead side by
// atan(tan(loft) sin(tilt)) (a right-hander's left), below them the other
// way. Rough's grass grabs the hosel: a little speed lost and the face
// closed, more with more loft and a longer swing. Off a tee
// the golfer strikes the ball in the face's middle; off the ground a face
// whose middle is higher than the ball's strikes it low.
delivery lie_delivery(const delivery& swing, const club_definition& club, const ball_on_lie& on, float backswing,
                      float ball_radius, const lie_tuning& tuning);

// What lies between the face and a ball `on` its lie: a teed ball has
// nothing, rough's grass cuts the face's grip (a flier), a wet lie's water
// film more, and sand is picked clean or exploded (impact.h sand_cushion).
lie_contact lie_contact_for(const ball_on_lie& on, const lie_tuning& tuning);
