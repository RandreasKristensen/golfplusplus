#include "game/shot_simulation.h"

#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// How high above the cup the ball centre may pass and still drop in.
float cup_capture_height(const ball_state& ball, const game_tuning& tuning) {
    return std::max(ball.radius * 4.0f, tuning.scale.ball_visual_radius_meters * 2.0f);
}

// Where a holed ball sits: down in the cup.
glm::vec3 in_cup_position(const shot_hole& hole, const ball_state& ball) {
    return hole.pin - glm::vec3(0.0f, ball.radius * 2.0f, 0.0f);
}

const club_definition* find_club(const std::vector<club_definition>& clubs, const std::string& id) {
    const auto found = std::find_if(clubs.begin(), clubs.end(), [&id](const club_definition& club) { return club.id == id; });
    return found != clubs.end() ? &*found : nullptr;
}

float trajectory_point_seconds() {
    return static_cast<float>(shot_steps_per_trajectory_point) * shot_step_seconds;
}
}

club_stats shot_club_stats(const club_stats& club, const bool cigarette_active, const reward_rules& rewards) {
    club_stats stats = club;
    if (cigarette_active) {
        stats.backspin *= rewards.cigarette.backspin_scale;
        stats.timing_speed *= rewards.cigarette.timing_speed_scale;
    }
    return stats;
}

ball_state launch_ball(const glm::vec3& position, const float aim_angle, const club_stats& stats, const float power,
                       const game_tuning& tuning) {
    const float min_power = tuning.swing.min_power;
    const float swing_power = std::isfinite(power) ? std::clamp(power, min_power, 1.0f) : min_power;
    const glm::vec3 forward = yaw_direction(aim_angle);
    const float loft = glm::radians(stats.loft_degrees);
    const glm::vec3 direction = glm::normalize(forward * std::cos(loft) + world_up * std::sin(loft));
    const float speed = stats.power * swing_power;

    ball_state ball;
    ball.position = position;
    ball.radius = tuning.scale.ball_physics_radius_meters;
    ball.velocity = direction * speed;
    // Backspin turns about the axis to the right of the shot (lift); side
    // spin turns about the shot direction itself (curve as the ball rises
    // and falls). Both follow the aim, so a shot flies the same either way.
    ball.spin = -yaw_left(forward) * (stats.backspin * speed) + forward * (stats.side_spin * tuning.swing.side_spin_scale);
    return ball;
}

shot_step step_shot(const ball_state& ball,
                    const terrain_sample& ground,
                    const shot_course& course,
                    const club_stats& stats,
                    const game_tuning& tuning,
                    const float wind_time,
                    const float dt) {
    const ball_tuning& rules = tuning.ball;
    const float roll_scale = std::max(0.0f, stats.roll_friction_scale);
    const bool was_airborne = !ball_is_grounded(ball, ground);
    const physics_tuning physics = ball_in_water(ball, ground, tuning.terrain.zones.water_depth)
        ? with_water_drag(tuning.physics)
        : tuning.physics;
    const wind_state wind = sample_wind(course.hole.wind_seed, wind_time, tuning.wind);

    shot_step step;
    step.ball = step_ball_flight(ball, wind, dt, physics);
    const terrain_sample after = sample_area(course.area, step.ball.position);
    const bool in_water = after.material == terrain_material::water;
    step.ball = resolve_terrain_collision(step.ball,
                                          after,
                                          in_water ? rules.water_restitution : rules.ground_restitution,
                                          in_water ? rules.water_friction : rules.ground_friction * roll_scale,
                                          dt);
    step.landed = was_airborne && ball_is_grounded(step.ball, after);

    const ball_state before_trees = step.ball;
    step.ball = resolve_tree_collisions(step.ball, course.trees, rules.tree_restitution, rules.tree_friction);
    step.hit_tree = glm::length(step.ball.velocity - before_trees.velocity) > 0.01f ||
        glm::length(step.ball.position - before_trees.position) > 0.001f;

    step.ball = apply_rolling_friction(step.ball, after, rules.roll_deceleration * roll_scale, rules.settle_speed, dt);
    step.ground = sample_area(course.area, step.ball.position);
    return step;
}

bool ball_at_rest(const ball_state& ball, const terrain_sample& ground, const ball_tuning& tuning) {
    return glm::length(ball.velocity) <= tuning.stop_speed && ball_is_grounded(ball, ground);
}

shot_result simulate_ball(const ball_state& launched,
                          const club_stats& stats,
                          const shot_course& course,
                          const game_tuning& tuning,
                          const float wind_time) {
    shot_result result;
    ball_state ball = launched;
    terrain_sample ground = sample_area(course.area, ball.position);
    result.trajectory.push_back(ball.position);

    const float cup_radius = tuning.scale.cup_radius_meters;
    int steps = 0;
    while (steps < shot_max_steps) {
        const glm::vec3 previous = ball.position;
        const float step_wind_time = wind_time + static_cast<float>(steps) * shot_step_seconds;
        const shot_step step = step_shot(ball, ground, course, stats, tuning, step_wind_time, shot_step_seconds);
        ++steps;
        ball = step.ball;
        ground = step.ground;

        const float time = static_cast<float>(steps) * shot_step_seconds;
        if (step.landed) {
            result.events.push_back(shot_event{time, shot_event_kind::land, ground.material});
        }
        if (step.hit_tree) {
            result.events.push_back(shot_event{time, shot_event_kind::tree_hit});
        }
        if (steps % shot_steps_per_trajectory_point == 0) {
            result.trajectory.push_back(ball.position);
        }
        if (path_crosses_cup(previous, ball.position, course.hole.pin, cup_radius, cup_capture_height(ball, tuning))) {
            result.holed = true;
            break;
        }
        if (ball_at_rest(ball, ground, tuning.ball)) {
            break;
        }
    }
    if (steps % shot_steps_per_trajectory_point != 0) {
        result.trajectory.push_back(ball.position);
    }

    result.duration = static_cast<float>(steps) * shot_step_seconds;
    result.holed = result.holed || horizontal_distance(ball.position, course.hole.pin) <= cup_radius;
    result.rest_position = result.holed ? in_cup_position(course.hole, ball) : ball.position;
    return result;
}

shot_result simulate_shot(const shot_input& input,
                          const shot_course& course,
                          const game_tuning& tuning,
                          const std::vector<club_definition>& clubs,
                          const reward_rules& rewards) {
    const club_definition* club = find_club(clubs, input.club_id);
    if (club == nullptr) {
        shot_result still;
        still.rest_position = input.ball_start;
        still.trajectory.push_back(input.ball_start);
        return still;
    }
    const club_stats stats = shot_club_stats(club->stats, input.cigarette_active, rewards);
    const ball_state launched = launch_ball(input.ball_start, input.aim_angle, stats, input.power, tuning);
    return simulate_ball(launched, stats, course, tuning, input.wind_time);
}

glm::vec3 shot_position_at(const shot_result& result, const float time) {
    const std::vector<glm::vec3>& points = result.trajectory;
    if (points.empty() || time >= result.duration) {
        return result.rest_position;
    }
    if (time <= 0.0f || points.size() == 1) {
        return points.front();
    }
    const float interval = trajectory_point_seconds();
    const std::size_t last = points.size() - 1;
    const std::size_t index = std::min(static_cast<std::size_t>(time / interval), last - 1);
    const float start = static_cast<float>(index) * interval;
    const float end = index + 1 == last ? result.duration : start + interval;
    const float t = end > start ? clamp01((time - start) / (end - start)) : 1.0f;
    return points[index] + (points[index + 1] - points[index]) * t;
}
