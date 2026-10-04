#include "play_rules.h"

#include "physics/vector_math.h"
#include "server_errors.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace {
bool is_mode(const std::uint8_t mode, const motion_mode wanted) {
    return mode == static_cast<std::uint8_t>(wanted);
}

bool in_cart(const std::uint8_t mode) {
    return is_mode(mode, motion_mode::cart) || is_mode(mode, motion_mode::drift);
}
}

std::optional<std::uint64_t> choose_room(const std::vector<room_candidate>& rooms, const int capacity) {
    std::optional<room_candidate> best;
    for (const room_candidate& room : rooms) {
        if (room.player_count >= capacity) {
            continue;
        }
        if (!best || room.player_count > best->player_count ||
            (room.player_count == best->player_count && room.room_id < best->room_id)) {
            best = room;
        }
    }
    return best ? std::optional<std::uint64_t>(best->room_id) : std::nullopt;
}

float mode_speed(const std::uint8_t mode, const game_tuning& tuning) {
    return in_cart(mode) ? fastest_cart_speed(tuning.cart) : tuning.player.walk_speed;
}

motion_check check_motion(const net_motion& previous,
                          const glm::vec3& to,
                          const float seconds,
                          const std::uint8_t mode,
                          const float slack_left,
                          const game_tuning& tuning) {
    // An update covers no more than max_motion_gap_seconds: standing still for
    // a while does not save up a jump.
    const float elapsed = std::clamp(seconds, 0.0f, tuning.server.max_motion_gap_seconds);
    const std::uint8_t previous_mode = static_cast<std::uint8_t>(previous.mode);
    const glm::vec3& from = previous.position;
    const float speed = std::max(mode_speed(previous_mode, tuning), mode_speed(mode, tuning));
    const float distance = horizontal_distance(from, to);
    const float beyond_speed = std::max(0.0f, distance - speed * tuning.server.motion_speed_scale * elapsed);

    motion_check check;
    check.allowed = beyond_speed <= slack_left;
    const float slack = tuning.server.motion_distance_slack;
    check.slack_left = std::min(slack, std::max(0.0f, slack_left - beyond_speed) + slack * elapsed);
    // The stretch was walked or driven as `previous` said: when it ended where
    // that motion leads, its path (turns included, as offline counts every
    // frame) is what was covered; otherwise only the straight line is known.
    const float path = std::abs(previous.speed) * elapsed;
    const float tolerance = tuning.net.motion_correction_distance + tuning.server.motion_distance_slack;
    const bool as_predicted = horizontal_distance(extrapolate_motion(previous, elapsed), to) <= tolerance;
    const float covered = as_predicted ? std::max(path, distance) : distance;
    check.earned_distance = check.allowed ? std::min(covered, mode_speed(previous_mode, tuning) * elapsed) : 0.0f;
    return check;
}

movement_earnings movement_earns(const std::uint8_t mode, const bool on_road) {
    movement_earnings earnings;
    earnings.walking = is_mode(mode, motion_mode::walk);
    earnings.cart = in_cart(mode) && on_road;
    earnings.drift = is_mode(mode, motion_mode::drift) && on_road;
    return earnings;
}

float clamp_motion_speed(const std::uint8_t mode, const float speed, const game_tuning& tuning) {
    const float limit = mode_speed(mode, tuning) * tuning.server.motion_speed_scale;
    return std::clamp(speed, -limit, limit);
}

float clamp_turn_rate(const float turn_rate, const game_tuning& tuning) {
    const float control = std::max({1.0f, tuning.cart.road_control_scale, tuning.cart.off_road_control_scale});
    const float limit = std::max({tuning.player.turn_rate, tuning.player.aim_turn_rate, tuning.cart.turn_rate * control,
                                  tuning.cart.drift_turn_rate * control});
    return std::clamp(turn_rate, -limit, limit);
}

bool within_interact_reach(const glm::vec3& position, const glm::vec3& target, const float radius,
                           const server_tuning& tuning) {
    return horizontal_distance(position, target) <= radius + tuning.interact_slack;
}

shot_check check_shot(const shot_request& request,
                      const server_ball& ball,
                      const glm::vec3& player_position,
                      const float seconds_on_hole,
                      const bool cigarette_lit,
                      const server_content& content) {
    shot_check check;
    if (request.stroke != ball.stroke_count + 1) {
        check.error = error_wrong_stroke;
        return check;
    }
    if (!within_interact_reach(player_position, ball.position, content.tuning.player.ball_interact_radius,
                               content.tuning.server)) {
        check.error = error_too_far;
        return check;
    }
    const bool known_club = std::any_of(content.clubs.begin(), content.clubs.end(),
                                        [&request](const club_definition& club) { return club.id == request.club_id; });
    if (!known_club) {
        check.error = error_unknown_club;
        return check;
    }
    const float slack = content.tuning.server.wind_time_slack_seconds;
    const float earliest_wind_time = std::max(ball.last_wind_time, seconds_on_hole - slack);
    // Aim comes wrapped (wrap_angle), so the server and every client take
    // the same sine and cosine of it.
    const bool aim_ok = std::isfinite(request.aim_angle) && std::abs(request.aim_angle) <= glm::pi<float>() + 0.0001f;
    if (!aim_ok || !std::isfinite(request.power) || !std::isfinite(request.wind_time) ||
        request.wind_time < earliest_wind_time || request.wind_time > seconds_on_hole + slack) {
        check.error = error_invalid_shot;
        return check;
    }
    // The client simulated the shot with its cigarette: a server whose
    // cigarette is out must say so, not simulate another shot.
    if (request.cigarette_active && !cigarette_lit) {
        check.error = error_cigarette_out;
        return check;
    }

    shot_input input;
    input.ball_start = ball.position;
    input.aim_angle = request.aim_angle;
    input.club_id = request.club_id;
    input.power = std::clamp(request.power, content.tuning.swing.min_power, 1.0f);
    input.cigarette_active = request.cigarette_active;
    input.wind_time = request.wind_time;
    check.input = input;
    return check;
}
