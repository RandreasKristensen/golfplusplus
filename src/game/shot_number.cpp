#include "game/shot_number.h"

#include "game/delivery.h"
#include "game/shot_ability.h"
#include "game/shot_simulation.h"
#include "physics/ball_physics.h"
#include "physics/flight_model.h"
#include "physics/launch.h"
#include "physics/turf_contact.h"
#include "physics/vector_math.h"

#include <cmath>

namespace {
// Where a ball struck as `strike` says first comes back down to the level
// it left from, in still `air`.
glm::vec3 landing(const strike_outcome& strike, const air_state& air, const game_tuning& tuning) {
    ball_state ball = launched_ball(glm::vec3(0.0f), tuning.scale.ball_physics_radius_meters, strike.launch);
    for (int step = 0; step < shot_max_steps; ++step) {
        const ball_state next = step_ball_flight(ball, wind_state{}, air, false, shot_step_seconds, tuning.physics);
        if (next.position.y < 0.0f) {
            const float share = ball.position.y / (ball.position.y - next.position.y);
            return ball.position + (next.position - ball.position) * share;
        }
        ball = next;
    }
    return ball.position;
}

// How far a putt struck as `strike` rolls on a level, dry green.
float roll(const strike_outcome& strike, const air_state& air, const float green_stimp_feet, const game_tuning& tuning) {
    const float radius = tuning.scale.ball_physics_radius_meters;
    terrain_sample green;
    green.material = terrain_material::green;
    green.inside_surface = true;
    green.triangle_index = 0;
    const turf surface = turf_for(terrain_material::green, 0.0f, green_stimp_feet, tuning);
    ball_state ball = launched_ball(glm::vec3(0.0f, radius, 0.0f), radius, strike.launch);
    for (int step = 0; step < shot_max_steps; ++step) {
        ball = step_ball_flight(ball, wind_state{}, air, false, shot_step_seconds, tuning.physics);
        green.point = glm::vec3(ball.position.x, 0.0f, ball.position.z);
        const turf_step met = turf_contact(ball, green, surface, tuning.ball.settle_speed, shot_step_seconds);
        ball = met.ball;
        if (met.resting && glm::length(ball.velocity) <= tuning.ball.stop_speed) {
            break;
        }
    }
    return horizontal_distance(ball.position, glm::vec3(0.0f));
}

// The club's own full swing: its stock face (with the golfer's lean),
// ball position and a backswing of at least a full one.
bool full_stock_swing(const shot_setup& setup, const club_definition& club, const ball_lie lie,
                      const golfer_profile& golfer, const game_tuning& tuning) {
    constexpr float same = 1.0e-4f;
    const shot_setup stock = stock_setup(club, lie, setup.ball_start, setup.aim_angle, setup.hand,
                                         natural_face_bias(club, golfer.natural, tuning.golfer), tuning.setup);
    return stock.backswing >= 1.0f && std::abs(setup.face_open - stock.face_open) < same &&
        std::abs(setup.ball_position - stock.ball_position) < same && std::abs(setup.backswing - stock.backswing) < same;
}
}

std::optional<shot_number> shot_number_for(const shot_record& record, const golfer_profile& golfer, const ball_lie lie,
                                           const float green_stimp_feet, const game_tuning& tuning,
                                           const std::vector<club_definition>& clubs) {
    const club_definition* club = find_club(clubs, record.setup.club_id);
    if (club == nullptr) {
        return std::nullopt;
    }
    const air_tuning& still = tuning.physics.air;
    const air_state air = air_at(still, 0.0f, still.standard_temperature - 273.15f);
    const ball_on_lie on{lie, stance_slope{}, 0.0f, record.setup.hand};
    const swing_errors errors = struck_errors(record.setup, record.seed, *club, lie, golfer, tuning);
    const strike_outcome struck = club_strike(*club, record.setup, on, tuning, errors);
    if (club->club_class == "putter") {
        return shot_number{roll(struck, air, green_stimp_feet, tuning), false};
    }
    const glm::vec3 landed = landing(struck, air, tuning);
    shot_number number{horizontal_distance(landed, glm::vec3(0.0f)), false};
    if (full_stock_swing(record.setup, *club, lie, golfer, tuning)) {
        const glm::vec3 planned = landing(club_strike(*club, record.setup, on, tuning), air, tuning);
        const float off = horizontal_distance(landed, planned);
        number.crit = off <= tuning.shot_numbers.crit_share * horizontal_distance(planned, glm::vec3(0.0f));
    }
    return number;
}
