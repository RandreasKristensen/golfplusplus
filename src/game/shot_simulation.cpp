#include "game/shot_simulation.h"

#include "game/tee_box.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Metres per cell of a shot's tree grid, a few canopies wide. Any size gives
// the same shot; only how many trees each step tests depends on it.
constexpr float tree_grid_cell_size = 16.0f;
// A ball drops when its centre passes over the cup slowly enough for it to
// fall in before it reaches the far side: the speed it may arrive at is
// greatest over the centre and least on the lip, where it has the shortest
// chord of the cup to fall into. A ball landing from the air is already
// falling, so a steep one may arrive faster (a dunk); a fast low one skips
// over, and one still in the air flies over. Judged on the ground, not at the
// cup's height, so a sloped green captures the same as a flat one.
bool drops_in_cup(const ball_state& before, const shot_step& step, const shot_hole& hole, const game_tuning& tuning) {
    const float radius = tuning.scale.cup_radius_meters;
    const float offset = path_cup_offset(before.position, step.ball.position, hole.pin);
    if (!ball_is_grounded(step.ball, step.ground) || radius <= 0.0f || offset > radius) {
        return false;
    }
    const float off_centre = offset / radius;
    const float chord = std::sqrt(std::max(0.0f, 1.0f - off_centre * off_centre));
    const float falling = step.landed ? std::max(0.0f, -before.velocity.y) : 0.0f;
    const ball_tuning& rules = tuning.ball;
    const float allowed = (rules.cup_capture_speed + rules.cup_dunk_scale * falling) *
        std::max(clamp01(rules.cup_lip_capture_scale), chord);
    return glm::length(horizontal(before.velocity)) <= allowed;
}

// Where a holed ball sits: down in the cup.
glm::vec3 in_cup_position(const shot_hole& hole, const ball_state& ball) {
    return hole.pin - glm::vec3(0.0f, ball.radius * 2.0f, 0.0f);
}

float trajectory_point_seconds() {
    return static_cast<float>(shot_steps_per_trajectory_point) * shot_step_seconds;
}
}

const club_definition* find_club(const std::vector<club_definition>& clubs, const std::string& id) {
    const auto found = std::find_if(clubs.begin(), clubs.end(), [&id](const club_definition& club) { return club.id == id; });
    return found != clubs.end() ? &*found : nullptr;
}

ball_lie lie_at(const play_area& area, const glm::vec3& position) {
    for (const tee_box& box : area.tee_boxes) {
        if (on_tee_box(box, position)) {
            return ball_lie::tee;
        }
    }
    switch (sample_area(area, position).material) {
    case terrain_material::fairway:
        return ball_lie::fairway;
    case terrain_material::rough:
        return ball_lie::rough;
    case terrain_material::green:
        return ball_lie::green;
    case terrain_material::bunker:
        return ball_lie::bunker;
    case terrain_material::water:
        return ball_lie::water;
    }
    return ball_lie::rough;
}

club_stats shot_club_stats(const club_stats& club, const bool cigarette_active, const reward_rules& rewards) {
    club_stats stats = club;
    if (cigarette_active) {
        stats.backspin *= rewards.cigarette.backspin_scale;
        stats.timing_speed *= rewards.cigarette.timing_speed_scale;
    }
    return stats;
}

club_stats lie_club_stats(const club_stats& club, const ball_lie lie, const game_tuning& tuning) {
    club_stats stats = club;
    const lie_tuning& from = tuning.lies[static_cast<std::size_t>(lie)];
    stats.power *= std::max(0.0f, from.power);
    stats.backspin *= std::max(0.0f, from.spin);
    // Sand takes most of the club's speed; wedges are made to get through it.
    if (lie == ball_lie::bunker) {
        stats.power *= clamp01(stats.bunker_power);
    }
    return stats;
}

ball_state launch_ball(const glm::vec3& position, const float aim_angle, const club_stats& stats, const float power,
                       const game_tuning& tuning) {
    const float min_power = tuning.swing.min_power;
    const float swing_power = std::isfinite(power) ? std::clamp(power, min_power, 1.0f) : min_power;
    const float curve = std::max(0.0f, tuning.swing.power_curve_exponent);
    const glm::vec3 forward = yaw_direction(aim_angle);
    const float loft = glm::radians(stats.loft_degrees);
    const glm::vec3 direction = glm::normalize(forward * std::cos(loft) + world_up * std::sin(loft));
    const float speed = stats.power * std::pow(swing_power, curve);

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

namespace {
// m/s^2 a rolling ball loses on `material`, before the club's roll scale.
float roll_deceleration(const ball_tuning& rules, const terrain_material material) {
    switch (material) {
    case terrain_material::green:
        return rules.green_roll_deceleration;
    case terrain_material::fairway:
        return rules.fairway_roll_deceleration;
    case terrain_material::rough:
        return rules.rough_roll_deceleration;
    case terrain_material::bunker:
        return rules.bunker_roll_deceleration;
    case terrain_material::water:
        break;
    }
    return 0.0f;  // water: the ball sinks instead (apply_rolling_friction)
}
}

shot_step step_shot(const ball_state& ball,
                    const terrain_sample& ground,
                    const shot_course& course,
                    const club_stats& stats,
                    const game_tuning& tuning,
                    const float wind_time,
                    const float dt,
                    const tree_grid* trees_near) {
    const ball_tuning& rules = tuning.ball;
    const bool was_airborne = !ball_is_grounded(ball, ground);
    const physics_tuning physics = ball_in_water(ball, ground)
        ? with_water_drag(tuning.physics)
        : tuning.physics;
    const wind_state wind = sample_wind(course.hole.wind_seed, wind_time, tuning.wind);

    shot_step step;
    step.ball = step_ball_flight(ball, wind, dt, physics);
    const terrain_sample after = sample_area(course.area, step.ball.position);
    const bool in_water = after.material == terrain_material::water;
    const bool in_sand = after.material == terrain_material::bunker;
    const float roll_scale = after.material == terrain_material::green ? std::max(0.0f, stats.roll_friction_scale) : 1.0f;
    step.ball = resolve_terrain_collision(step.ball,
                                          after,
                                          in_water ? rules.water_restitution
                                              : in_sand ? rules.bunker_restitution
                                              : rules.ground_restitution,
                                          in_water ? rules.water_friction
                                              : in_sand ? rules.bunker_friction
                                              : rules.ground_friction * roll_scale,
                                          dt);
    step.landed = was_airborne && ball_is_grounded(step.ball, after);

    const ball_state before_trees = step.ball;
    step.ball = trees_near != nullptr
        ? resolve_tree_collisions(step.ball, course.trees, *trees_near, rules.tree_restitution, rules.tree_friction)
        : resolve_tree_collisions(step.ball, course.trees, rules.tree_restitution, rules.tree_friction);
    step.ball = resolve_tree_collisions(step.ball, course.area.sign_posts, rules.tree_restitution, rules.tree_friction);
    step.ball = resolve_tree_collisions(step.ball, course.area.fence_poles, rules.tree_restitution, rules.tree_friction);
    step.hit_tree = glm::length(step.ball.velocity - before_trees.velocity) > 0.01f ||
        glm::length(step.ball.position - before_trees.position) > 0.001f;
    // Swept from where the step began, so a fast ball cannot pass through.
    step.ball = resolve_fence_collisions(ball.position, step.ball, course.area.fence_panels,
                                         tuning.fence.net_restitution, tuning.fence.net_friction);

    step.ball = apply_rolling_friction(step.ball, after,
                                       roll_deceleration(rules, after.material) * roll_scale,
                                       rules.settle_speed, dt);
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
    const tree_grid trees_near = build_tree_grid(course.trees, tree_grid_cell_size);

    const float cup_radius = tuning.scale.cup_radius_meters;
    const int linger_steps = static_cast<int>(std::ceil(std::max(0.0f, tuning.ball.water_linger_seconds) / shot_step_seconds));
    int on_pond_floor = -1;  // steps since a lost ball reached the floor
    int steps = 0;
    while (steps < shot_max_steps) {
        const ball_state before = ball;
        const float step_wind_time = wind_time + static_cast<float>(steps) * shot_step_seconds;
        const shot_step step =
            step_shot(ball, ground, course, stats, tuning, step_wind_time, shot_step_seconds, &trees_near);
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
        if (drops_in_cup(before, step, course.hole, tuning)) {
            result.holed = true;
            break;
        }
        if (ball_in_water(ball, ground) && ball.position.y + ball.radius < ground.water_level) {
            result.penalty_strokes = 1;  // under: lost
        }
        if (result.penalty_strokes > 0) {
            on_pond_floor = on_pond_floor >= 0 || ball_is_grounded(ball, ground) ? on_pond_floor + 1 : -1;
            if (on_pond_floor >= linger_steps) {
                break;
            }
            continue;
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
    result.rest_position = result.holed ? in_cup_position(course.hole, ball)
        : result.penalty_strokes > 0 ? launched.position
        : ball.position;
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
    const club_stats stats = lie_club_stats(shot_club_stats(club->stats, input.cigarette_active, rewards),
                                            lie_at(course.area, input.ball_start), tuning);
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
