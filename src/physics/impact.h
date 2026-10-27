#pragma once

// The strike: how a club's head meeting the ball launches it (launch.h),
// by oblique-impact physics (Penner 2001, 'The physics of golf: the optimum
// loft of a driver'; Cross & Nathan): the head (with the share of the shaft
// and hands it carries) and the ball trade momentum along the face's normal
// by the face's restitution, and along the face by its grip, the ball
// rolling up the face and leaving with backspin. The D-plane (TrackMan)
// gives where it starts and how its spin axis tilts. Pure.

#include "physics/launch.h"

#include <optional>

// A club's build and how it is swung at skill 99 (its item's `spec`,
// assets/play/items/club/). Angles in degrees, lengths in metres.
struct club_spec {
    float static_loft_degrees = 0.0f;
    float lie_degrees = 0.0f;
    float length_meters = 0.0f;
    float effective_mass_kg = 0.0f;  // the head, with the shaft's and hands' share at impact
    float face_cor = 0.0f;           // restitution along the face's normal
    float face_grip = 0.0f;          // restitution along the face: how much the ball's spin builds
    // Ball on face, sliding: the most the face can drag the ball along it,
    // for each unit it drives it off (tests/fixtures/reference/impact.json).
    float face_friction = 0.0f;
    float face_share = 0.0f;         // of the face's angle (against the path's) the ball starts along
    // The one named correction a class may carry (docs/plan.md 5.5.3): the
    // driver's tour strike above the face's middle takes spin off (its
    // vertical gear effect), which a flat face does not model.
    float spin_scale = 1.0f;
    // Gear effect (woods): radians the spin axis tilts per metre struck
    // towards the toe, and the share of backspin gained per metre struck
    // below the face's middle.
    float gear_effect = 0.0f;
    float face_centre_height_meters = 0.0f;  // the face's middle over the sole
    float delivered_loft_degrees = 0.0f;  // the face's loft at impact with the stock setup
    float speed_at_99 = 0.0f;             // m/s, a full stock swing
    float stock_attack_degrees = 0.0f;    // positive rising
    float stock_ball_position = 0.0f;
    float stock_backswing = 1.0f;
    float sole_bounce_degrees = 0.0f;
    float forgiveness = 0.0f;  // 0..1: how little an off-centre strike costs
    float max_face_open_degrees = 0.0f;
    float max_face_closed_degrees = 0.0f;
    float sand_face_open_degrees = 0.0f;  // how far its stock setup opens the face from sand
};

// What the club does at impact. Angles in radians; directions are world yaw
// (vector_math.h yaw_direction).
struct delivery {
    float club_speed = 0.0f;    // m/s
    float path = 0.0f;          // the head's travel, world yaw
    float attack = 0.0f;        // the head's travel up (+) or down (-)
    float face = 0.0f;          // where the face points, world yaw
    float dynamic_loft = 0.0f;  // the face's loft at impact
    float strike_toe = 0.0f;    // metres from the face's middle towards the toe (- heel)
    float strike_high = 0.0f;   // metres above it (- low)
};

// How sand takes a club that enters it behind the ball (the explosion):
// the ball leaves on a cushion of sand at speed_share of the club's speed
// times the sine of the face's loft (a low face ploughs the sand on instead
// of throwing it up), with spin_share of a clean strike's spin. The club
// enters the sand behind the ball when its sole leads its edge into the
// sand (its bounce against how steeply it comes down) and it is still
// coming down; fully past entry_blend_degrees and descent_blend_degrees.
struct sand_cushion {
    float speed_share = 0.0f;
    float spin_share = 0.0f;
    float entry_blend_degrees = 0.0f;
    float descent_blend_degrees = 0.0f;
};

// What the ball is struck from: the share of the face's grip (spin) left
// by what lies between them (grass, water, sand), the ground's rise along
// the path (radians) and, in a bunker, its sand.
struct lie_contact {
    float grip_share = 1.0f;
    float ground_slope = 0.0f;
    std::optional<sand_cushion> sand;
};

struct strike_outcome {
    launch_conditions launch;
    float centredness = 1.0f;  // 1 struck in the middle, less off it (which sound)
};

// The ball, of `ball_mass` kg and `ball_radius` m, launched by `club` as
// `swing` delivers it from `lie`.
strike_outcome impact(const club_spec& club, const delivery& swing, const lie_contact& lie, float ball_mass, float ball_radius);
