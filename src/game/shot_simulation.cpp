#include "game/shot_simulation.h"

#include "game/area_solids.h"
#include "game/shot_ability.h"
#include "game/tee_box.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/cup.h"
#include "physics/det_math.h"
#include "physics/ground_contact.h"
#include "physics/solid.h"
#include "physics/vector_math.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
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

stance_slope stance_at(const play_area& area, const glm::vec3& ball, const float aim, const handedness hand,
                       const game_tuning& tuning) {
    const lie_tuning& lies = tuning.lies;
    const float unit = tuning.scale.meters_per_world_unit;
    const float out = lies.stance_ball_distance_meters / unit;
    const float width = lies.stance_width_meters / unit;
    const glm::vec3 forward = yaw_direction(aim);
    const glm::vec3 middle = ball + yaw_left(forward) * (out * hand_sign(hand));
    const float lead = sample_area(area, middle + forward * (0.5f * width)).point.y;
    const float trail = sample_area(area, middle - forward * (0.5f * width)).point.y;
    const float under_ball = sample_area(area, ball).point.y;
    stance_slope slope;
    slope.ball_above_feet = det_atan2(under_ball - 0.5f * (lead + trail), out);
    slope.uphill = det_atan2(lead - trail, width);
    return slope;
}

ball_on_lie ball_on_lie_at(const play_area& area, const glm::vec3& ball, const float aim, const handedness hand,
                           const float wetness, const game_tuning& tuning) {
    return ball_on_lie{lie_at(area, ball), stance_at(area, ball, aim, hand, tuning), wetness, hand};
}

strike_outcome club_strike(const club_definition& club, const shot_setup& setup, const ball_on_lie& on,
                           const game_tuning& tuning, const swing_errors& errors) {
    const shot_setup held = clamped_setup(setup, setup_limits_for(club, tuning.setup));
    const float ball_radius = tuning.scale.ball_physics_radius_meters;
    delivery swung = club_delivery(club, held, tuning.swing);
    swung.club_speed *= 1.0f + errors.speed_share;
    swung.path += errors.path;
    swung.face += errors.path + errors.face;
    swung.strike_toe += errors.strike_toe;
    swung.strike_high += errors.strike_high;
    const delivery swing = lie_delivery(swung, club, on, held.backswing, ball_radius, tuning.lies);
    return impact(club.spec, swing, lie_contact_for(on, tuning.lies), tuning.physics.aero.mass, ball_radius);
}

ball_state launch_ball(const club_definition& club, const shot_setup& setup, const ball_on_lie& on,
                       const game_tuning& tuning) {
    return launched_ball(setup.ball_start, tuning.scale.ball_physics_radius_meters,
                         club_strike(club, setup, on, tuning).launch);
}

air_state shot_air(const shot_course& course, const game_tuning& tuning) {
    return air_at(tuning.physics.air, course.elevation, course.weather.temperature_c);
}

shot_course make_shot_course(const play_area& area, const std::size_t hole, const weather_state& weather,
                             const course_definition& course, const std::vector<bool>& flags_in,
                             const game_tuning& tuning) {
    const glm::vec3 pin = hole < area.cups.size() ? area.cups[hole].centre : glm::vec3(0.0f);
    shot_course made{area, shot_hole{pin, hole}, weather, course.air.elevation_meters, course.green_stimp_feet, {}, std::nullopt, 0};
    made.dynamic = flagstick_solids(area.cups, flags_in, tuning);
    return made;
}

std::uint64_t shot_seed(const shot_setup& setup) {
    // FNV-1a over what the player chose, bit for bit.
    std::uint64_t hash = 14695981039346656037ull;
    const auto mix = [&hash](const void* data, const std::size_t size) {
        const unsigned char* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash = (hash ^ bytes[i]) * 1099511628211ull;
        }
    };
    const float numbers[] = {setup.ball_start.x, setup.ball_start.y, setup.ball_start.z, setup.aim_angle,
                             setup.face_open,    setup.ball_position, setup.backswing};
    mix(numbers, sizeof(numbers));
    mix(&setup.hand, sizeof(setup.hand));
    mix(setup.club_id.data(), setup.club_id.size());
    mix(&setup.shot_at, sizeof(setup.shot_at));
    return hash;
}

turf turf_for(const terrain_material material, const float wetness, const float green_stimp_feet,
              const game_tuning& tuning) {
    const surfaces_tuning& surfaces = tuning.surfaces;
    const surface_tuning& surface = material == terrain_material::fairway ? surfaces.fairway
        : material == terrain_material::green                            ? surfaces.green
        : material == terrain_material::bunker                           ? surfaces.bunker
                                                                         : surfaces.rough;
    const float wet = clamp01(wetness);
    const auto blend = [wet](const float dry, const float soaked_share) { return dry * (1.0f + (soaked_share - 1.0f) * wet); };
    turf out = surface.dry;
    if (material == terrain_material::green) {
        out.stimp_feet = green_stimp_feet;
    }
    out.stimp_feet = blend(out.stimp_feet, surface.wet_stimp_scale);
    out.contact_angle_scale = blend(out.contact_angle_scale, surface.wet_contact_angle_scale);
    out.restitution_scale = blend(out.restitution_scale, surface.wet_restitution_scale);
    out.friction = blend(out.friction, surface.wet_friction_scale);
    out.plug_speed = blend(out.plug_speed, surface.wet_plug_speed_scale);
    return out;
}

namespace {
// How high the ball flies over what is under it: over a pond, its surface.
float height_over_ground(const ball_state& ball, const terrain_sample& ground) {
    const float surface = ground.material == terrain_material::water ? std::max(ground.point.y, ground.water_level)
                                                                     : ground.point.y;
    return ball.position.y - surface;
}

shot_event_kind event_of(const solid_kind kind) {
    switch (kind) {
    case solid_kind::trunk:
    case solid_kind::leaves:
        return shot_event_kind::tree_hit;
    case solid_kind::board:
    case solid_kind::tee_box:
    case solid_kind::cart:
        return shot_event_kind::board_hit;
    case solid_kind::flagstick:
        return shot_event_kind::flag_hit;
    case solid_kind::person:
        return shot_event_kind::person_hit;
    case solid_kind::ball:
        return shot_event_kind::ball_hit;
    case solid_kind::pole:
    case solid_kind::net:
    case solid_kind::lamp:
        break;
    }
    return shot_event_kind::pole_hit;
}

// Through `metres` of foliage: slowed, and turned by up to the canopy's
// scatter about a random axis across its path.
ball_state through_leaves(const ball_state& in, const float metres, const shot_course& course, const int step,
                          const solids_tuning& tuning) {
    ball_state out = in;
    const float speed = glm::length(in.velocity);
    if (speed <= 0.0f) {
        return out;
    }
    const glm::vec3 heading = in.velocity / speed;
    const glm::vec3 side = safe_normalize(glm::cross(heading, world_up), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 lift = glm::cross(side, heading);
    const float around = glm::two_pi<float>() * dice_uniform(course.seed, step, 0);
    const float turn = glm::radians(tuning.leaves_scatter_degrees_per_meter * metres) * (2.0f * dice_uniform(course.seed, step, 1) - 1.0f);
    const glm::vec3 axis = side * det_cos(around) + lift * det_sin(around);
    const glm::vec3 turned = safe_normalize(heading * det_cos(turn) + glm::cross(axis, heading) * det_sin(turn), heading);
    out.velocity = turned * (speed * det_exp(-tuning.leaves_drag_per_meter * metres));
    return out;
}

// The cup whose hole is under `ball`, if any.
std::optional<std::size_t> cup_over(const ball_state& ball, const std::vector<cup>& cups) {
    for (std::size_t i = 0; i < cups.size(); ++i) {
        if (over_cup(ball, cups[i])) {
            return i;
        }
    }
    return std::nullopt;
}
}

shot_step step_shot(const ball_state& ball,
                    const terrain_sample& ground,
                    const shot_course& course,
                    const game_tuning& tuning,
                    const float dt,
                    const int step_number) {
    const ball_tuning& rules = tuning.ball;
    const play_area& area = course.area;
    const weather_state& weather = course.weather;
    const wind_state wind = wind_at_height(weather.wind, height_over_ground(ball, ground), tuning.wind.profile);

    shot_step step;
    step.ball = step_ball_flight(ball, wind, shot_air(course, tuning), ball_in_water(ball, ground), dt, tuning.physics);
    const terrain_sample after = sample_area(area, step.ball.position);
    const std::optional<std::size_t> over = cup_over(step.ball, area.cups);
    if (after.material == terrain_material::water) {
        step.ball = resolve_terrain_collision(step.ball, after, rules.water_restitution, rules.water_friction, dt);
    } else if (!over) {
        const turf_step met =
            turf_contact(step.ball, after, turf_for(after.material, weather.wetness, course.green_stimp_feet, tuning),
                         rules.settle_speed, dt);
        step.ball = met.ball;
        step.landed = met.bounced && !ball_is_grounded(ball, ground);
        step.plugged = met.plugged;
        step.resting = met.resting;
    }

    // The cups near it: rim, wall and bottom.
    for (std::size_t i = 0; i < area.cups.size(); ++i) {
        const cup_step in_cup_step = cup_contact(step.ball, area.cups[i], tuning.cup.contact);
        if (in_cup_step.touched) {
            step.ball = in_cup_step.ball;
            if (in_cup_step.on_rim && over) {
                // Riding the lip it still rolls on the green's grass, losing speed as it does there.
                const glm::vec3 level = horizontal(step.ball.velocity);
                const float speed = glm::length(level);
                if (speed > 0.0f) {
                    const float lost = std::min(speed, rolling_deceleration(speed, course.green_stimp_feet) * dt /
                                                           tuning.scale.meters_per_world_unit);
                    step.ball.velocity -= level * (lost / speed);
                }
            }
            step.cup = i;
            step.resting = in_cup_step.supported && glm::length(step.ball.velocity) <= rules.stop_speed;
        }
    }
    if (over && !step.cup) {
        step.cup = over;
    }

    // Walls are swept from where the step began, so a fast ball cannot pass through.
    // The one who hit it only once the grace is over.
    const float since_hit = static_cast<float>(step_number + 1) * dt;
    const std::size_t met_count = course.shooter && since_hit < tuning.solids.self_hit_grace_seconds
        ? *course.shooter
        : course.dynamic.size();
    const ball_contacts met =
        resolve_ball_contacts(ball.position, step.ball, area.solids, area.solids_grid, course.dynamic, met_count);
    step.ball = met.ball;
    if (met.hit) {
        step.hit = event_of(*met.hit);
        step.hit_dynamic = met.moving_hit;
        step.resting = false;
    }
    const float foliage = porous_path(ball.position, step.ball.position, area.solids, area.solids_grid);
    if (foliage > 0.0f) {
        step.ball = through_leaves(step.ball, foliage, course, step_number, tuning.solids);
        step.through_leaves = true;
    }
    step.ground = sample_area(area, step.ball.position);
    return step;
}

shot_result simulate_ball(const ball_state& launched, const shot_course& course, const game_tuning& tuning) {
    shot_result result;
    ball_state ball = launched;
    terrain_sample ground = sample_area(course.area, ball.position);
    result.trajectory.push_back(ball.position);

    const int linger_steps = static_cast<int>(std::ceil(std::max(0.0f, tuning.ball.water_linger_seconds) / shot_step_seconds));
    int on_pond_floor = -1;  // steps since a lost ball reached the floor
    int steps = 0;
    bool in_leaves = false;
    std::optional<shot_event_kind> touching;
    std::optional<std::size_t> touching_cup;
    // Where the ball was as it first came over each cup it went into.
    std::vector<glm::vec3> came_in(course.area.cups.size(), glm::vec3(0.0f));
    std::optional<std::size_t> resting_cup;
    const bool from_green = ground.material == terrain_material::green;
    bool hit_ball_on_green = false;
    while (steps < shot_max_steps) {
        const ball_state before = ball;
        const shot_step step = step_shot(ball, ground, course, tuning, shot_step_seconds, steps);
        ++steps;
        ball = step.ball;
        ground = step.ground;

        const float time = static_cast<float>(steps) * shot_step_seconds;
        if (step.landed) {
            result.events.push_back(shot_event{time, shot_event_kind::land, ground.material});
        }
        if (step.hit && step.hit != touching) {
            result.events.push_back(shot_event{time, *step.hit});
        }
        if (from_green && step.hit == shot_event_kind::ball_hit && step.hit_dynamic) {
            const solid& other = course.dynamic[*step.hit_dynamic];
            hit_ball_on_green = hit_ball_on_green || sample_area(course.area, other.base).material == terrain_material::green;
        }
        touching = step.hit;
        if (step.through_leaves && !in_leaves) {
            result.events.push_back(shot_event{time, shot_event_kind::leaves});
        }
        in_leaves = step.through_leaves;
        if (step.cup && step.cup != touching_cup) {
            result.events.push_back(shot_event{time, shot_event_kind::cup_rattle});
            came_in[*step.cup] = before.position;
        }
        touching_cup = step.cup;
        if (steps % shot_steps_per_trajectory_point == 0) {
            result.trajectory.push_back(ball.position);
        }
        if (ball_in_water(ball, ground) && ball.position.y + ball.radius < ground.water_level) {
            result.lost = true;
        }
        if (result.lost) {
            on_pond_floor = on_pond_floor >= 0 || ball_is_grounded(ball, ground) ? on_pond_floor + 1 : -1;
            if (on_pond_floor >= linger_steps) {
                break;
            }
            continue;
        }
        if (step.plugged) {
            break;
        }
        if (step.resting && glm::length(ball.velocity) <= tuning.ball.stop_speed) {
            if (step.cup && in_cup(ball, course.area.cups[*step.cup])) {
                resting_cup = step.cup;
            }
            break;
        }
    }
    if (steps % shot_steps_per_trajectory_point != 0) {
        result.trajectory.push_back(ball.position);
    }

    result.duration = static_cast<float>(steps) * shot_step_seconds;
    result.holed = resting_cup && *resting_cup == course.hole.index;
    result.rest_position = result.lost ? launched.position : ball.position;
    result.penalty_strokes = (result.lost ? 1 : 0) + (hit_ball_on_green ? 2 : 0);
    if (resting_cup && !result.holed) {
        // Lifted out of another green's cup and dropped beside it.
        const cup& other = course.area.cups[*resting_cup];
        const glm::vec3 side = safe_normalize(horizontal(came_in[*resting_cup] - other.centre), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 drop = other.centre + side * (tuning.cup.wrong_cup_drop_distance_meters / tuning.scale.meters_per_world_unit);
        result.rest_position = resting_ball_position(course.area, drop, ball.radius);
    }
    return result;
}

shot_result simulate_shot(const shot_setup& setup,
                          const shot_course& course,
                          const game_tuning& tuning,
                          const std::vector<club_definition>& clubs,
                          const swing_errors& errors) {
    const club_definition* club = find_club(clubs, setup.club_id);
    if (club == nullptr) {
        shot_result still;
        still.rest_position = setup.ball_start;
        still.trajectory.push_back(setup.ball_start);
        return still;
    }
    const shot_setup held = clamped_setup(setup, setup_limits_for(*club, tuning.setup));
    const ball_on_lie on =
        ball_on_lie_at(course.area, held.ball_start, held.aim_angle, held.hand, course.weather.wetness, tuning);
    const strike_outcome strike = club_strike(*club, held, on, tuning, errors);
    shot_result result =
        simulate_ball(launched_ball(held.ball_start, tuning.scale.ball_physics_radius_meters, strike.launch), course, tuning);
    result.strike_centredness = strike.centredness;
    return result;
}

shot_result simulate_struck_shot(const shot_record& record,
                                 const golfer_profile& golfer,
                                 const shot_course& course,
                                 const game_tuning& tuning,
                                 const std::vector<club_definition>& clubs) {
    shot_course seeded = course;
    seeded.seed = record.seed;
    const std::vector<solid> moving = snapshot_solids(record.snapshot, tuning);
    const bool has_shooter = std::any_of(record.snapshot.begin(), record.snapshot.end(),
                                         [](const snapshot_entry& entry) { return entry.shooter; });
    seeded.dynamic.insert(seeded.dynamic.end(), moving.begin(), moving.end());
    if (has_shooter) {
        seeded.shooter = seeded.dynamic.size() - 1;
    }
    const club_definition* club = find_club(clubs, record.setup.club_id);
    if (club == nullptr) {
        return simulate_shot(record.setup, seeded, tuning, clubs);
    }
    const shot_ability ability =
        ability_for(record.setup, *club, lie_at(course.area, record.setup.ball_start), golfer, tuning);
    return simulate_shot(record.setup, seeded, tuning, clubs, rolled_errors(record.seed, ability.spread));
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
