#include "physics/impact.h"

#include "physics/det_math.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec3.hpp>

strike_outcome impact(const club_spec& club, const delivery& swing, const lie_contact& lie, const float ball_mass,
                      const float ball_radius) {
    strike_outcome out;
    const float mass = std::max(0.001f, club.effective_mass_kg);
    // The face against the head's travel: up and down (vertical spin loft)
    // and side to side (face to path); together the spin loft.
    const float vertical = swing.dynamic_loft - swing.attack;
    const float sideways = swing.face - swing.path;
    const float spin_loft = std::sqrt(vertical * vertical + sideways * sideways);

    // Off the middle the face gives less: by the square of the miss, less so
    // for a forgiving club.
    const float miss = std::sqrt(swing.strike_toe * swing.strike_toe + swing.strike_high * swing.strike_high);
    constexpr float miss_scale = 0.02f;  // metres: a miss this far costs (1 - forgiveness) x 0.5 of the restitution
    const float off = miss / miss_scale;
    out.centredness = 1.0f / (1.0f + off * off);
    const float restitution = club.face_cor * (1.0f - 0.5f * (1.0f - club.forgiveness) * (1.0f - out.centredness));

    const float normal = (1.0f + restitution) * mass / (mass + ball_mass) * swing.club_speed * det_cos(spin_loft);
    // The ball's speed along the face, gripped until it rolls (grass between
    // them takes most of it: a flier, sliding, spinning less). The face can
    // only drag it so far for how hard it drives it off: struck very
    // obliquely (a wide open lob wedge) the ball slides all the way, leaving
    // nearer the face's normal, higher and with less spin.
    const float rolling = (1.0f + club.face_grip * lie.grip_share) * swing.club_speed * det_sin(spin_loft) /
        (3.5f + ball_mass / mass) * lie.grip_share;
    const float along = std::min(rolling, club.face_friction * lie.grip_share * normal);
    const float speed = std::sqrt(normal * normal + along * along);
    // Dragged along the face it leaves off the face's normal, tipped towards
    // the head's travel in the plane of the two (path along +z here).
    const float below_normal = det_atan2(along, normal);
    const glm::vec3 face_normal(det_cos(swing.dynamic_loft) * det_sin(sideways), det_sin(swing.dynamic_loft),
                                det_cos(swing.dynamic_loft) * det_cos(sideways));
    const glm::vec3 travel(0.0f, det_sin(swing.attack), det_cos(swing.attack));
    const glm::vec3 across = travel - face_normal * glm::dot(travel, face_normal);
    const float across_length = glm::length(across);
    const glm::vec3 leaves = across_length > 1e-6f
        ? face_normal * det_cos(below_normal) + across * (det_sin(below_normal) / across_length)
        : face_normal;

    out.launch.ball_speed = speed;
    out.launch.launch_angle = det_atan2(leaves.y, std::sqrt(leaves.x * leaves.x + leaves.z * leaves.z));
    out.launch.direction = swing.path + club.face_share * sideways;
    // A wood struck low spins more, high less (gear effect).
    const float gear_spin = std::max(0.0f, 1.0f - club.gear_effect * swing.strike_high);
    out.launch.spin = 2.5f * along / std::max(0.0001f, ball_radius) * club.spin_scale * gear_spin;
    // The D-plane: face against path, over the vertical spin loft, tilts the
    // spin's axis; a wood struck towards its toe turns it back (gear effect).
    out.launch.spin_axis_tilt = det_atan2(sideways, std::max(0.0001f, vertical)) + club.gear_effect * swing.strike_toe;

    if (lie.sand) {
        // Against the sand's surface: the sole's bounce (less the shaft's
        // forward lean, more for a face opened or the ball forward) and how
        // steeply the club comes down.
        const float loft_over_ground = swing.dynamic_loft - lie.ground_slope;
        const float bounce = glm::radians(club.sole_bounce_degrees - club.static_loft_degrees) + loft_over_ground;
        const float descent = lie.ground_slope - swing.attack;
        const float explosion = clamp01((bounce - descent) / glm::radians(lie.sand->entry_blend_degrees)) *
            clamp01(descent / glm::radians(lie.sand->descent_blend_degrees));
        const float cushion = swing.club_speed * lie.sand->speed_share * det_sin(std::max(0.0f, loft_over_ground));
        out.launch.ball_speed += (cushion - out.launch.ball_speed) * explosion;
        out.launch.spin *= 1.0f + (lie.sand->spin_share - 1.0f) * explosion;
    }
    return out;
}
