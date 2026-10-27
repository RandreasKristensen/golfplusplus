#include "game/delivery.h"

#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/trigonometric.hpp>

float backswing_speed_share(const club_definition& club, const float backswing, const swing_tuning& tuning) {
    const tuning_curve& curve = club.club_class == "putter" ? tuning.putting_speed_share : tuning.speed_share;
    return curve.at(std::max(0.0f, backswing));
}

float face_open_loft_gain(const float face_open, const float lie_degrees) {
    const float gain = det_sin(face_open) * det_cos(glm::radians(lie_degrees));
    // asin of a share below 1 in magnitude, by atan2 (det_math has no asin).
    return det_atan2(gain, std::sqrt(std::max(0.0f, 1.0f - gain * gain)));
}

delivery club_delivery(const club_definition& club, const shot_setup& setup, const swing_tuning& tuning) {
    const club_spec& spec = club.spec;
    delivery swing;
    swing.club_speed = spec.speed_at_99 * backswing_speed_share(club, setup.backswing, tuning);
    swing.path = setup.aim_angle;
    // The stock delivery is the club's at its own ball position.
    const float moved = setup.ball_position - spec.stock_ball_position;
    swing.attack = glm::radians(spec.stock_attack_degrees + tuning.ball_position_attack_degrees * moved);
    swing.face = world_face_yaw(setup);
    swing.dynamic_loft = glm::radians(spec.delivered_loft_degrees + tuning.ball_position_loft_degrees * moved) +
        face_open_loft_gain(setup.face_open, spec.lie_degrees);
    return swing;
}

delivery lie_delivery(const delivery& swing, const club_definition& club, const ball_on_lie& on, const float backswing,
                      const float ball_radius, const lie_tuning& tuning) {
    const club_spec& spec = club.spec;
    delivery out = swing;
    const float tilt = on.stance.ball_above_feet;
    const float lead_side = hand_sign(on.hand);
    out.face += lead_side * det_atan2(det_sin(swing.dynamic_loft) * det_sin(tilt), det_cos(swing.dynamic_loft));
    out.dynamic_loft += on.stance.uphill;
    out.attack += on.stance.uphill;
    // A ball lying in water is played as from soaked rough.
    if (on.lie == ball_lie::rough || on.lie == ball_lie::water) {
        out.club_speed *= 1.0f - tuning.rough_speed_loss;
        out.face += lead_side * glm::radians(tuning.rough_face_close_per_loft * spec.static_loft_degrees) *
            std::max(0.0f, backswing);
    }
    if (on.lie != ball_lie::tee) {
        out.strike_high += std::min(0.0f, ball_radius - spec.face_centre_height_meters);
    }
    return out;
}

lie_contact lie_contact_for(const ball_on_lie& on, const lie_tuning& tuning) {
    lie_contact contact;
    contact.ground_slope = on.stance.uphill;
    float wetness = clamp01(on.wetness);
    switch (on.lie) {
    case ball_lie::tee:
        return contact;
    case ball_lie::bunker:
        contact.grip_share = tuning.sand_grip_share;
        contact.sand = tuning.sand;
        return contact;
    case ball_lie::water:
        wetness = 1.0f;
        contact.grip_share = tuning.rough_grip_share;
        break;
    case ball_lie::rough:
        contact.grip_share = tuning.rough_grip_share;
        break;
    case ball_lie::fairway:
    case ball_lie::green:
        break;
    }
    contact.grip_share *= std::max(0.0f, 1.0f - tuning.wet_grip_loss * wetness);
    return contact;
}
