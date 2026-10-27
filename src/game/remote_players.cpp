#include "game/remote_players.h"

#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "game/player_jump.h"
#include "game/shot_simulation.h"
#include "game/swing.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

namespace {
bool connected(const game_state& state) {
    return state.online.status == net_status::connected;
}

void follow(game_state& state, remote_avatar& avatar, const std::uint64_t account, const room_player& player, const bool known,
            const float dt) {
    const net_tuning& tuning = state.tuning.net;
    const net_motion& motion = *player.motion;
    const float since = state.online.server_now != 0
        ? std::clamp(server_seconds_between(player.motion_at, state.online.server_now), 0.0f, tuning.remote_extrapolation_seconds)
        : 0.0f;
    glm::vec3 target = extrapolate_motion(motion, since);
    float yaw = wrap_angle(motion.yaw + motion.turn_rate * since);
    // Riding along: in the seat of the cart as it is shown.
    const std::optional<std::uint64_t> ridden = motion.mode == motion_mode::ride ? cart_ridden_by(state, account) : std::nullopt;
    const std::optional<cart_pose> cart = ridden ? shown_cart(state, *ridden) : std::nullopt;
    if (cart) {
        target = seat_position(*cart, cart_seat::passenger, state.tuning.cart);
        yaw = cart->yaw;
    }
    const float surface = terrain_height(state.area, target);
    target.y = surface;
    // A motion sent from the air: they hopped. Half the height, as the ground
    // sampled again may differ by a hair.
    const player_tuning& player_moves = state.tuning.player;
    const bool on_foot = motion.mode == motion_mode::walk || motion.mode == motion_mode::idle;
    const bool hopping = (!known || avatar.motion_at != player.motion_at) && on_foot &&
        motion.position.y - terrain_height(state.area, motion.position) > 0.5f * airborne_height;

    if (!known) {
        avatar.position = target;
        avatar.motion_at = player.motion_at;
    } else if (avatar.motion_at != player.motion_at) {
        // Stepping into or out of a cart blends over the step.
        const bool steps = in_cart(avatar.mode) != in_cart(motion.mode);
        avatar.correction = avatar.position - target;
        // On foot their height is their own up and down.
        if (on_foot) {
            avatar.correction.y = 0.0f;
        }
        avatar.correction_seconds = steps ? state.tuning.cart.step_seconds : tuning.remote_correction_seconds;
        avatar.correction_left = avatar.correction_seconds;
        avatar.motion_at = player.motion_at;
    }
    avatar.correction_left = std::max(0.0f, avatar.correction_left - dt);
    const float blend = avatar.correction_seconds > 0.0f ? avatar.correction_left / avatar.correction_seconds : 0.0f;
    const float shown_y = known ? avatar.position.y : surface;
    avatar.position = target + avatar.correction * blend;
    if (on_foot) {
        const vertical_body taking_off = hopping ? hopped(avatar.vertical, player_moves) : avatar.vertical;
        avatar.vertical = settle_on_foot(taking_off, shown_y, terrain_height(state.area, avatar.position), player_moves, dt);
        avatar.position.y = avatar.vertical.y;
    } else {
        avatar.vertical = vertical_body{};
    }
    avatar.yaw = yaw;
    avatar.mode = motion.mode;
    tick_item_use(avatar.smoking, state.items, dt);
    tick_item_use(avatar.drinking, state.items, dt);
}

void append_trail_point(std::vector<glm::vec3>& trail, const glm::vec3& point, const float min_spacing) {
    if (trail.empty() || glm::length(point - trail.back()) >= min_spacing) {
        trail.push_back(point);
    }
}
}

void update_remote_players(game_state& state, const float dt) {
    if (!is_online(state) || !connected(state)) {
        return;  // frozen until the server is back
    }
    for (const auto& [account, player] : state.online.players) {
        if (account == state.online.account_id || !player.motion) {
            continue;
        }
        const bool known = state.remote_avatars.count(account) != 0;
        follow(state, state.remote_avatars[account], account, player, known, dt);
    }
    for (auto it = state.remote_avatars.begin(); it != state.remote_avatars.end();) {
        const auto player = state.online.players.find(it->first);
        const bool gone = player == state.online.players.end() || !player->second.motion || it->first == state.online.account_id;
        it = gone ? state.remote_avatars.erase(it) : std::next(it);
    }

    const float fade = state.tuning.net.remote_trail_fade_seconds;
    for (auto it = state.remote_shots.begin(); it != state.remote_shots.end();) {
        remote_shot& shot = it->second;
        const bool resting = shot.elapsed >= shot.result.duration;
        shot.elapsed += dt;
        shot.next_event = push_shot_events(state, shot.result, shot.correction, shot.next_event, shot.elapsed);
        if (shot.result.holed && !resting && shot.elapsed >= shot.result.duration) {
            push_audio_event(state, audio_event_type::ball_cup, remote_shot_position(shot));
        }
        append_trail_point(shot.trail, remote_shot_position(shot), state.tuning.flight_path.min_point_spacing);
        it = shot.elapsed >= shot.result.duration + fade ? state.remote_shots.erase(it) : std::next(it);
    }
}

shot_result room_shot_result(const game_state& state, const room_shot& shot) {
    std::vector<bool> flags_in(state.area.cups.size(), true);
    for (const int out : shot.flags_out) {
        if (out >= 0 && static_cast<std::size_t>(out) < flags_in.size()) {
            flags_in[static_cast<std::size_t>(out)] = false;
        }
    }
    const shot_course course = make_shot_course(state.area, static_cast<std::size_t>(std::max(0, shot.hole)),
                                                course_weather_at(state, shot.record.setup.shot_at), state.course, flags_in,
                                                state.tuning);
    return simulate_struck_shot(shot.record, shot.golfer, course, state.tuning, state.clubs);
}

void start_remote_shot(game_state& state, const room_shot& shot) {
    // Their game sent where they stood just before the shot (launch_shot), so
    // the motion I have is the one it was hit from.
    const auto avatar = state.remote_avatars.find(shot.account_id);
    const auto player = state.online.players.find(shot.account_id);
    if (avatar != state.remote_avatars.end() && player != state.online.players.end()) {
        avatar->second.hit_motion_at = player->second.motion_at;
    }
    if (shot.hole < 0 || static_cast<std::size_t>(shot.hole) >= state.holes.size()) {
        return;
    }
    const shot_setup& setup = shot.record.setup;
    remote_shot playing;
    playing.input = setup;
    playing.result = room_shot_result(state, shot);
    playing.correction = shot.rest - playing.result.rest_position;
    playing.trail.push_back(remote_shot_position(playing));
    if (const club_definition* club = find_club(state.clubs, setup.club_id)) {
        push_club_hit(state, *club, playing.result, setup.ball_start);
    }
    state.remote_shots[shot.account_id] = std::move(playing);
}

void start_remote_item_use(game_state& state, const room_item_use& use) {
    const auto avatar = state.remote_avatars.find(use.account_id);
    const item_definition* item = state.items.find(use.item_id);
    if (avatar == state.remote_avatars.end() || item == nullptr || !item->use) {
        return;  // not shown (yet), or nothing to show
    }
    std::optional<item_use_shown>& playing =
        item->kind == item_kind::smokable ? avatar->second.smoking : avatar->second.drinking;
    if (!playing) {
        playing = item_use_shown{item->id, 0.0f};
        push_item_use(state, *item, avatar->second.position);
    }
}

std::optional<remote_address> remote_address_pose(const game_state& state, const std::uint64_t account) {
    const auto avatar = state.remote_avatars.find(account);
    const auto player = state.online.players.find(account);
    const auto ball = state.online.balls.find(account);
    if (avatar == state.remote_avatars.end() || player == state.online.players.end() || ball == state.online.balls.end() ||
        !player->second.motion) {
        return std::nullopt;
    }
    const net_motion& motion = *player->second.motion;
    const bool addressing = motion.mode == motion_mode::address || motion.mode == motion_mode::swing;
    if (!addressing || player->second.motion_at == avatar->second.hit_motion_at) {
        return std::nullopt;
    }
    remote_address address{ball->second.position, motion.yaw, 0.0f};
    if (motion.mode == motion_mode::swing) {
        const float since = state.online.server_now != 0
            ? std::max(0.0f, server_seconds_between(player->second.motion_at, state.online.server_now))
            : 0.0f;
        const swing_tempo tempo{state.tuning.swing.backswing_seconds, state.tuning.swing.downswing_seconds};
        address.club_raise = swing_club_raise(since, 1.0f, tempo);
    }
    return address;
}

glm::vec3 remote_shot_position(const remote_shot& shot) {
    const float duration = shot.result.duration;
    const float progress = duration > 0.0f ? std::clamp(shot.elapsed / duration, 0.0f, 1.0f) : 1.0f;
    return shot_position_at(shot.result, std::clamp(shot.elapsed, 0.0f, duration)) + shot.correction * progress;
}

bool remote_shot_playing(const remote_shot& shot) {
    return shot.elapsed < shot.result.duration;
}

std::vector<shown_ball> remote_balls(const game_state& state) {
    std::vector<shown_ball> balls;
    for (const auto& [account, shot] : state.remote_shots) {
        if (remote_shot_playing(shot)) {
            balls.push_back(shown_ball{account, remote_shot_position(shot)});
        }
    }
    for (const auto& [account, ball] : state.online.balls) {
        const auto shot = state.remote_shots.find(account);
        const bool playing = shot != state.remote_shots.end() && remote_shot_playing(shot->second);
        if (account != state.online.account_id && !playing) {
            balls.push_back(shown_ball{account, ball.position});
        }
    }
    return balls;
}

player_relationship relationship_to(const game_state& state, const room_player& player) {
    const room_player* me = my_room_player(state);
    if (me != nullptr && me->group_id != 0 && player.group_id == me->group_id) {
        return player_relationship::grouped;
    }
    return player_relationship::unknown;
}

std::optional<std::uint64_t> nearest_player(const game_state& state, const float distance) {
    std::optional<std::uint64_t> best;
    float best_distance = std::numeric_limits<float>::max();
    for (const auto& [account, avatar] : state.remote_avatars) {
        if (state.online.players.find(account) == state.online.players.end()) {
            continue;
        }
        const float apart = horizontal_distance(avatar.position, state.player.position);
        if (apart <= distance && apart < best_distance) {
            best = account;
            best_distance = apart;
        }
    }
    return best;
}
