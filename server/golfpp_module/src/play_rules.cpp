#include "play_rules.h"

#include "account_rules.h"
#include "game/shot_ability.h"
#include "physics/det_math.h"
#include "physics/vector_math.h"
#include "game/server_errors.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

std::optional<std::uint64_t> choose_room(const std::vector<room_candidate>& rooms, const std::uint32_t world_id) {
    std::optional<std::uint64_t> chosen;
    for (const room_candidate& room : rooms) {
        if (room.world_id == world_id && (!chosen || room.room_id < *chosen)) {
            chosen = room.room_id;
        }
    }
    return chosen;
}

std::vector<int> tee_round_strokes(const std::vector<int>& strokes, const std::size_t hole_count) {
    std::vector<int> next = strokes;
    next.resize(hole_count, 0);
    if (!next.empty() && std::all_of(next.begin(), next.end(), [](const int score) { return score > 0; })) {
        std::fill(next.begin(), next.end(), 0);
    }
    return next;
}

movement_limits movement_limits_for(const game_tuning& tuning, const cart_spec& cart,
                                    const std::vector<active_effect>& effects, const std::int64_t now_ms) {
    movement_limits limits;
    limits.foot_speed =
        effective_value(tuning.player.walk_speed, effect_target_walk_speed, effects, now_ms) * tuning.player.run_factor;
    cart_spec moving = cart;
    moving.top_speed_mps = effective_value(cart.top_speed_mps, effect_target_cart_top_speed, effects, now_ms);
    const float top_speed = game_top_speed(moving, tuning.cart);
    limits.cart_speed = top_speed / tuning.scale.meters_per_world_unit;
    const float lock = glm::radians(cart.max_steer_degrees);
    const float cart_turn = top_speed * det_sin(lock) / det_cos(lock) / cart.wheelbase_m;
    limits.turn_rate = std::max(tuning.player.turn_rate, cart_turn);
    return limits;
}

float mode_speed(const std::uint8_t mode, const movement_limits& limits) {
    return in_cart(static_cast<motion_mode>(mode)) ? limits.cart_speed : limits.foot_speed;
}

motion_check check_motion(const net_motion& previous,
                          const glm::vec3& to,
                          const float seconds,
                          const std::uint8_t mode,
                          const float slack_left,
                          const movement_limits& limits,
                          const game_tuning& tuning) {
    // An update covers no more than max_motion_gap_seconds: standing still for
    // a while does not save up a jump.
    const float elapsed = std::clamp(seconds, 0.0f, tuning.server.max_motion_gap_seconds);
    const std::uint8_t previous_mode = static_cast<std::uint8_t>(previous.mode);
    const glm::vec3& from = previous.position;
    const float speed = std::max(mode_speed(previous_mode, limits), mode_speed(mode, limits));
    const float distance = horizontal_distance(from, to);
    const float beyond_speed = std::max(0.0f, distance - speed * tuning.server.motion_speed_scale * elapsed);

    motion_check check;
    check.allowed = beyond_speed <= slack_left;
    const float slack = tuning.server.motion_distance_slack;
    check.slack_left = std::min(slack, std::max(0.0f, slack_left - beyond_speed) + slack * elapsed);
    return check;
}

float clamp_motion_speed(const std::uint8_t mode, const float speed, const movement_limits& limits,
                         const server_tuning& tuning) {
    const float limit = mode_speed(mode, limits) * tuning.motion_speed_scale;
    return std::clamp(speed, -limit, limit);
}

bool ride_motion_plausible(const glm::vec3& last, const float seconds, const glm::vec3& to, const movement_limits& limits,
                           const server_tuning& tuning) {
    const float gap = std::clamp(seconds, 0.0f, tuning.max_motion_gap_seconds);
    return horizontal_distance(last, to) <= limits.cart_speed * tuning.motion_speed_scale * gap + tuning.motion_distance_slack;
}

float clamp_turn_rate(const float turn_rate, const movement_limits& limits) {
    return std::clamp(turn_rate, -limits.turn_rate, limits.turn_rate);
}

bool within_interact_reach(const glm::vec3& position, const glm::vec3& target, const float radius,
                           const server_tuning& tuning) {
    return horizontal_distance(position, target) <= radius + tuning.interact_slack;
}

bool within_seat_reach(const cart_pose& cart, const cart_seat seat, const glm::vec3& position, const game_tuning& tuning) {
    return within_interact_reach(position, seat_position(cart, seat, tuning.cart), tuning.cart.board_reach, tuning.server);
}

std::size_t free_parking_slot(const std::vector<glm::vec3>& carts, const glm::vec3& start, const glm::vec3& pin,
                              const cart_tuning& tuning) {
    const float clear = tuning.park_spacing * 0.5f;
    for (std::size_t slot = 0;; ++slot) {
        const glm::vec3 spot = parked_cart_pose(start, pin, slot, tuning).position;
        const bool taken = std::any_of(carts.begin(), carts.end(),
                                       [&](const glm::vec3& cart) { return horizontal_distance(cart, spot) < clear; });
        if (!taken) {
            return slot;
        }
    }
}

std::string check_item_use(const std::string& item_id, const loadout& equipped, const std::int64_t last_used_ms,
                           const std::int64_t now_ms, const server_content& content) {
    const item_definition* item = content.items.find(item_id);
    if (item == nullptr || !item->use) {
        return error_unknown_item;
    }
    const std::string& slot = item->kind == item_kind::smokable ? equipped.smokable : equipped.drinkable;
    if (slot != item_id) {
        return error_not_equipped;
    }
    constexpr float milliseconds_per_second = 1000.0f;
    const auto slack = static_cast<std::int64_t>(content.tuning.server.timing_slack_seconds * milliseconds_per_second);
    if (item_use_playing(*item, last_used_ms, now_ms + slack)) {
        return error_too_soon;
    }
    return {};
}

shot_check check_shot(const shot_request& request,
                      const server_ball& ball,
                      const glm::vec3& player_position,
                      const server_golfer& golfer,
                      const std::int64_t now_unix_ms,
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
    const club_definition* club = find_club(content.clubs, request.club_id);
    if (club == nullptr) {
        check.error = error_unknown_club;
        return check;
    }
    if (request.club_id != golfer.club_in_hand) {
        check.error = error_club_not_in_hand;
        return check;
    }
    if (golfer.bag_on_back) {
        check.error = error_bag_on_back;
        return check;
    }
    std::vector<int> theirs = request.flags_out;
    std::vector<int> ours = golfer.flags_out;
    std::sort(theirs.begin(), theirs.end());
    std::sort(ours.begin(), ours.end());
    if (theirs != ours) {
        check.error = error_flag_changed;
        return check;
    }
    constexpr float milliseconds_per_second = 1000.0f;
    const std::int64_t slack =
        static_cast<std::int64_t>(content.tuning.server.shot_time_slack_seconds * milliseconds_per_second);
    const std::int64_t earliest_shot_at = std::max(ball.last_shot_at, now_unix_ms - slack);
    shot_setup input;
    input.ball_start = ball.position;
    input.club_id = request.club_id;
    input.aim_angle = request.aim_angle;
    input.face_open = request.face_open;
    input.ball_position = request.ball_position;
    input.backswing = request.backswing;
    input.strike_timing = request.strike_timing;
    input.hand = golfer.golfer.hand;
    input.shot_at = request.shot_at;
    // Aim comes wrapped (wrap_angle), so the server and every client take
    // the same sine and cosine of it; the dials as the golfer can set them.
    const setup_limits limits = ability_for(input, *club, ball.lie, golfer.golfer, content.tuning).limits;
    constexpr float dial_slack = 0.0001f;
    const auto within = [](const float value, const float low, const float high) {
        return std::isfinite(value) && value >= low - dial_slack && value <= high + dial_slack;
    };
    const bool dials_ok = within(request.aim_angle, -glm::pi<float>(), glm::pi<float>()) &&
        within(request.face_open, limits.face_open_min, limits.face_open_max) &&
        within(request.ball_position, limits.ball_position_min, limits.ball_position_max) &&
        within(request.backswing, limits.backswing_min, limits.backswing_max) &&
        within(request.strike_timing, -1.0f, 1.0f);
    if (!dials_ok || request.shot_at < earliest_shot_at || request.shot_at > now_unix_ms + slack) {
        check.error = error_invalid_shot;
        return check;
    }
    check.input = clamped_setup(input, limits);
    return check;
}

std::uint64_t server_shot_seed(const std::string& secret, const std::int64_t timestamp_micros, const std::uint64_t account_id,
                               const int stroke) {
    return round_shot_seed(keyed_hash(secret, std::to_string(timestamp_micros) + ":" + std::to_string(account_id)), stroke);
}

std::string check_bag_move(const bag_place& from, const bag_place& to, const glm::vec3& player,
                           const std::optional<cart_pose>& from_cart, const std::optional<cart_pose>& to_cart,
                           const bool to_spot_taken, const game_tuning& tuning) {
    const float reach = tuning.player.bag_reach;
    const auto reaches = [&](const glm::vec3& target) { return within_interact_reach(player, target, reach, tuning.server); };
    const bool finite = std::isfinite(to.position.x) && std::isfinite(to.position.y) && std::isfinite(to.position.z) &&
        std::isfinite(to.yaw);
    if (!finite) {
        return error_invalid_motion;
    }
    if ((!from_cart && from.where == bag_where::on_cart) || (!to_cart && to.where == bag_where::on_cart)) {
        return error_bag_out_of_reach;
    }
    if (from.where == bag_where::on_back) {
        switch (to.where) {
        case bag_where::on_cart:
            if (to_spot_taken) {
                return error_bag_spot_taken;
            }
            return reaches(bag_spot(*to_cart, to.seat, tuning.cart)) ? std::string() : std::string(error_bag_out_of_reach);
        case bag_where::on_ground:
            return reaches(to.position) ? std::string() : std::string(error_bag_out_of_reach);
        case bag_where::on_back:
            break;
        }
        return error_invalid_motion;
    }
    if (to.where != bag_where::on_back) {
        return error_invalid_motion;
    }
    return reaches(bag_at(from, from_cart.value_or(cart_pose{}), player, tuning.cart)) ? std::string()
                                                                                         : std::string(error_bag_out_of_reach);
}

std::string check_take_club(const std::string& club_id, const bag_place& bag, const glm::vec3& player,
                            const std::optional<cart_pose>& cart, const server_content& content) {
    if (find_club(content.clubs, club_id) == nullptr) {
        return error_unknown_club;
    }
    if (bag.where == bag_where::on_back) {
        return error_bag_on_back;
    }
    if (bag.where == bag_where::on_cart && !cart) {
        return error_bag_out_of_reach;
    }
    const game_tuning& tuning = content.tuning;
    return within_interact_reach(player, bag_at(bag, cart.value_or(cart_pose{}), player, tuning.cart), tuning.player.bag_reach,
                                 tuning.server)
        ? std::string()
        : std::string(error_bag_out_of_reach);
}
