#include "movement_rules.h"

#include "game/cart_seats.h"
#include "game/play_area.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

namespace {
bool finite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool valid(const move_request& request, const server_tuning& tuning) {
    const net_motion& m = request.motion;
    if (static_cast<std::uint8_t>(m.mode) >= motion_mode_count || !finite(m.position) || !finite(request.start) ||
        !std::isfinite(m.yaw) || !std::isfinite(m.speed) || !std::isfinite(m.turn_rate) || !std::isfinite(m.client_time)) {
        return false;
    }
    std::uint64_t steps = 0;
    for (const move_run& run : request.runs) {
        if (run.steps == 0 || static_cast<std::uint8_t>(run.input.kind) > static_cast<std::uint8_t>(move_kind::cart)) {
            return false;
        }
        steps += run.steps;
    }
    return steps <= max_steps_per_motion(tuning);
}

// Standing on the surface at `feet`, as the game puts a player down.
glm::vec3 on_ground(const play_area& area, const glm::vec3& feet) {
    return glm::vec3(feet.x, terrain_height(area, feet), feet.z);
}

// The speed a claimed move in `mode` costs its time at.
float claim_speed(const motion_mode from, const motion_mode to, const move_world& world) {
    const float speed = std::max(mode_speed(static_cast<std::uint8_t>(from), world.limits),
                                 mode_speed(static_cast<std::uint8_t>(to), world.limits));
    return std::max(speed * world.tuning.server.motion_speed_scale, 0.001f);
}

// A move to `to` no step made: allowed within the time saved up and the
// slack (check_motion), spending the time it takes.
bool claim(mover_state& m, const glm::vec3& to, const motion_mode mode, const move_world& world) {
    net_motion previous;
    previous.mode = m.mode;
    previous.position = m.position;
    const motion_check check = check_motion(previous, to, m.credit_seconds, static_cast<std::uint8_t>(mode), m.slack_left,
                                            world.limits, world.tuning);
    if (!check.allowed) {
        return false;
    }
    const float spent = horizontal_distance(m.position, to) / claim_speed(m.mode, mode, world);
    m.credit_seconds = std::max(0.0f, m.credit_seconds - spent);
    m.slack_left = check.slack_left;
    m.position = to;
    m.vertical = vertical_body{to.y, 0.0f, true};
    return true;
}

// Off my cart's wheel, standing out of its driver's door.
void leave_wheel(mover_state& m, room_cart& cart, const move_world& world) {
    cart.driver = 0;
    m = placed_mover(m, on_ground(world.area, step_out_position(cart_pose{cart.position, cart.yaw}, cart_seat::driver,
                                                                world.tuning.cart)));
}
}

std::uint32_t max_steps_per_motion(const server_tuning& tuning) {
    return static_cast<std::uint32_t>(2.0f * tuning.max_motion_gap_seconds / move_step_seconds);
}

mover_state placed_mover(mover_state mover, const glm::vec3& position) {
    mover.position = position;
    mover.vertical = vertical_body{position.y, 0.0f, true};
    mover.cart_velocity = glm::vec2(0.0f);
    mover.cart_yaw_rate = 0.0f;
    mover.cart_lift_seconds = 0.0f;
    mover.cart_vertical = vertical_body{};
    return mover;
}

move_outcome play_move(const mover_state& mover, const std::optional<room_cart>& cart, const std::uint64_t account,
                       const move_request& request, const float seconds, const move_world& world) {
    move_outcome out;
    out.mover = mover;
    const game_tuning& tuning = world.tuning;
    if (!valid(request, tuning.server)) {
        out.error = error_invalid_motion;
        return out;
    }
    const net_motion& asked = request.motion;
    if (asked.client_time == mover.client_time && mover.client_time != 0.0) {
        out.repeated = true;
        return out;
    }
    if (asked.client_time < mover.client_time) {
        out.error = error_invalid_motion;
        return out;
    }
    mover_state& m = out.mover;
    m.credit_seconds = std::min(tuning.server.max_motion_gap_seconds, m.credit_seconds + std::max(0.0f, seconds));
    std::optional<room_cart> my_cart = cart;
    bool driving = my_cart && my_cart->driver == account && account != 0;
    cart_mover body;
    if (my_cart) {
        body.pose = cart_pose{my_cart->position, my_cart->yaw};
        body.motion.yaw = my_cart->yaw;
        body.motion.velocity = m.cart_velocity;
        body.motion.yaw_rate = m.cart_yaw_rate;
        body.motion.lift_seconds = m.cart_lift_seconds;
        body.motion.condition = my_cart->condition;
        body.vertical = m.cart_vertical;
        body.vertical.y = my_cart->position.y;
    }
    const walk_world walking{world.area, world.walker_obstacles, tuning.player, world.walk_speed, nullptr};
    const cart_world driving_world{world.area, tuning, world.spec, world.wetness, world.grip_scale,
                                   world.cart_obstacles, nullptr};
    // My cart's row and my mover as its steps so far leave them.
    const auto commit_cart = [&]() {
        my_cart->position = body.pose.position;
        my_cart->yaw = body.pose.yaw;
        my_cart->condition = body.motion.condition;
        my_cart->motion_id = asked.client_time;
        m.cart_velocity = body.motion.velocity;
        m.cart_yaw_rate = body.motion.yaw_rate;
        m.cart_lift_seconds = body.motion.lift_seconds;
        m.cart_vertical = body.vertical;
    };
    float walked_speed = 0.0f;
    float walked_angle = 0.0f;
    std::optional<std::uint16_t> walked_yaw;
    bool first_walk = true;
    std::uint64_t number = request.first_step;
    for (const move_run& run : request.runs) {
        const move_input& input = run.input;
        if (input.kind == move_kind::cart && (!my_cart || my_cart->driver != account || account == 0)) {
            out = move_outcome{};
            out.mover = mover;
            out.error = error_not_in_cart;
            return out;
        }
        for (std::uint32_t i = 0; i < run.steps; ++i, ++number) {
            if (number <= m.step) {
                continue;  // played already
            }
            m.step = number;
            if (m.credit_seconds < move_step_seconds) {
                continue;  // more steps than time: passed over
            }
            m.credit_seconds -= move_step_seconds;
            if (input.kind == move_kind::walk) {
                if (driving) {
                    commit_cart();
                    leave_wheel(m, *my_cart, world);
                    driving = false;
                    out.cart = my_cart;
                }
                if (first_walk && number == request.first_step && request.start != m.position) {
                    const float off = horizontal_distance(request.start, m.position);
                    const float spent = off / claim_speed(motion_mode::walk, motion_mode::walk, world);
                    if (off <= tuning.server.step_start_tolerance && spent <= m.credit_seconds) {
                        m.credit_seconds -= spent;
                        m.position = request.start;
                        m.vertical.y = request.start.y;
                    }
                }
                first_walk = false;
                const glm::vec3 from = m.position;
                const walk_step walked = step_walker(walker{m.position, m.vertical, 0.0f, 0.0f, false}, input, walking);
                m.position = walked.after.position;
                m.vertical = walked.after.vertical;
                walked_speed = walked.after.speed;
                walked_angle = walked.after.move_angle;
                walked_yaw = input.yaw;
                out.walked_meters += horizontal_distance(from, m.position) * tuning.scale.meters_per_world_unit;
                continue;
            }
            if (body.motion.condition != cart_condition::upright) {
                continue;
            }
            const glm::vec3 from = body.pose.position;
            const bool on_road = on_cart_road(world.roads, from, tuning.cart);
            const cart_move moved = step_cart_mover(body, cart_controls{static_cast<float>(input.forward), steer_of(input.steer)},
                                                    driving_world);
            body = moved.after;
            if (on_road) {
                out.road_meters += horizontal_distance(from, body.pose.position) * tuning.scale.meters_per_world_unit;
            }
            if (body.motion.condition != cart_condition::upright) {
                // Over under me: out of it beside its driver's door, and its rider out too.
                out.overturned = true;
                commit_cart();
                my_cart->driver = 0;
                my_cart->passenger = 0;
                m = placed_mover(m, on_ground(world.area, step_out_position(body.pose, cart_seat::driver, tuning.cart)));
                driving = false;
                out.cart = my_cart;
            }
        }
    }

    if (my_cart && driving) {
        commit_cart();
        out.cart = my_cart;
        m.position = on_ground(world.area, seat_position(body.pose, cart_seat::driver, tuning.cart));
        m.vertical = vertical_body{m.position.y, 0.0f, true};
    }

    motion_mode mode = asked.mode;
    net_motion shown = asked;
    if (mode == motion_mode::cart && !out.overturned) {
        if (!driving) {
            out = move_outcome{};
            out.mover = mover;
            out.error = error_not_in_cart;
            return out;
        }
        shown.position = m.position;
        shown.yaw = body.pose.yaw;
        shown.speed = cart_forward_speed(body.motion) * tuning.cart.time_scale / tuning.scale.meters_per_world_unit;
        shown.turn_rate = body.motion.yaw_rate * tuning.cart.time_scale;
    } else {
        if (mode == motion_mode::cart) {
            mode = motion_mode::idle;  // tumbled out
        }
        if (driving) {
            leave_wheel(m, *my_cart, world);
            out.cart = my_cart;
        }
        if (mode == motion_mode::walk || mode == motion_mode::idle) {
            // Where the steps left me; a claim on foot moves nothing.
            shown.position = m.position;
            if (walked_yaw) {
                shown.yaw = wrap_angle(yaw_of(*walked_yaw) + walked_angle);
            }
            shown.speed = walked_speed;
        } else if (!claim(m, asked.position, mode, world)) {
            const bool at_own_ball = at_ball(mode) && world.own_ball &&
                within_interact_reach(m.position, *world.own_ball, tuning.player.ball_interact_radius, tuning.server) &&
                within_interact_reach(asked.position, *world.own_ball, tuning.player.ball_interact_radius, tuning.server);
            if (!at_own_ball) {
                out = move_outcome{};
                out.mover = mover;
                out.error = error_too_fast;
                return out;
            }
            m.position = asked.position;
            m.vertical = vertical_body{asked.position.y, 0.0f, true};
        }
    }
    shown.mode = mode;
    shown.speed = clamp_motion_speed(static_cast<std::uint8_t>(mode), shown.speed, world.limits, tuning.server);
    shown.turn_rate = clamp_turn_rate(shown.turn_rate, world.limits);
    shown.client_time = asked.client_time;
    m.mode = mode;
    m.client_time = asked.client_time;
    out.shown = shown;
    return out;
}

float earning_meters(const float meters, float& used, const float per_hour) {
    const float earning = std::clamp(per_hour - used, 0.0f, std::max(0.0f, meters));
    used += earning;
    return earning;
}
