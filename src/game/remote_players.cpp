#include "game/remote_players.h"

#include "game/mode_dispatch.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "game/swing.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// Others' meters run at this speed: the club they hold isn't sent.
constexpr float base_meter_speed = 1.0f;

bool connected(const game_state& state) {
    return state.online.status == net_status::connected;
}

void follow(game_state& state, remote_avatar& avatar, const room_player& player, const bool known, const float dt) {
    const net_tuning& tuning = state.tuning.net;
    const net_motion& motion = *player.motion;
    const float since = state.online.server_now != 0
        ? std::clamp(server_seconds_between(player.motion_at, state.online.server_now), 0.0f, tuning.remote_extrapolation_seconds)
        : 0.0f;
    glm::vec3 target = extrapolate_motion(motion, since);
    target.y = terrain_height(state.area, target);

    if (!known) {
        avatar.position = target;
        avatar.motion_at = player.motion_at;
    } else if (avatar.motion_at != player.motion_at) {
        avatar.correction = avatar.position - target;
        avatar.correction_left = tuning.remote_correction_seconds;
        avatar.motion_at = player.motion_at;
    }
    avatar.correction_left = std::max(0.0f, avatar.correction_left - dt);
    const float blend = tuning.remote_correction_seconds > 0.0f ? avatar.correction_left / tuning.remote_correction_seconds : 0.0f;
    avatar.position = target + avatar.correction * blend;
    avatar.yaw = wrap_angle(motion.yaw + motion.turn_rate * since);
    avatar.mode = motion.mode;
    tick_emote(avatar.smoke_emote, state.tuning.player.emote_seconds, dt);
    tick_emote(avatar.drink_emote, state.tuning.player.emote_seconds, dt);
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
        follow(state, state.remote_avatars[account], player, known, dt);
    }
    for (auto it = state.remote_avatars.begin(); it != state.remote_avatars.end();) {
        const auto player = state.online.players.find(it->first);
        const bool gone = player == state.online.players.end() || !player->second.motion || it->first == state.online.account_id;
        it = gone ? state.remote_avatars.erase(it) : std::next(it);
    }

    const float fade = state.tuning.net.remote_trail_fade_seconds;
    for (auto it = state.remote_shots.begin(); it != state.remote_shots.end();) {
        remote_shot& shot = it->second;
        shot.elapsed += dt;
        append_trail_point(shot.trail, remote_shot_position(shot), state.tuning.flight_path.min_point_spacing);
        it = shot.elapsed >= shot.result.duration + fade ? state.remote_shots.erase(it) : std::next(it);
    }
}

void start_remote_shot(game_state& state, const room_shot& shot) {
    // Their game sent where they stood just before the shot (launch_shot), so
    // the motion I have is the one it was hit from.
    const auto avatar = state.remote_avatars.find(shot.account_id);
    const auto player = state.online.players.find(shot.account_id);
    if (avatar != state.remote_avatars.end() && player != state.online.players.end()) {
        avatar->second.hit_motion_at = player->second.motion_at;
    }
    if (!state.hub || shot.zone < 0 || static_cast<std::size_t>(shot.zone) >= state.hub->markers.size()) {
        return;
    }
    const hub_hole_marker& marker = state.hub->markers[static_cast<std::size_t>(shot.zone)];
    const shot_course course{state.area, state.static_anchors.trees,
                             shot_hole{anchor_on_terrain(state.area, marker.pin_position), marker.wind_seed}};
    remote_shot playing;
    playing.input = shot.input;
    playing.result = simulate_shot(shot.input, course, state.tuning, state.clubs, state.rewards);
    playing.correction = shot.rest - playing.result.rest_position;
    playing.trail.push_back(remote_shot_position(playing));
    state.remote_shots[shot.account_id] = std::move(playing);
}

void start_remote_emote(game_state& state, const room_emote& emote) {
    const auto avatar = state.remote_avatars.find(emote.account_id);
    if (avatar == state.remote_avatars.end()) {
        return;  // not shown (yet)
    }
    emote_state& playing = emote.emote == emote_id::smoke ? avatar->second.smoke_emote : avatar->second.drink_emote;
    if (!playing.active) {
        trigger_emote(playing);
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
        address.club_power = swing_meter_power(since, base_meter_speed, state.tuning.swing.meter_cycle_seconds);
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
        const auto player = state.online.players.find(account);
        if (player == state.online.players.end() || player->second.zone != local_zone(state)) {
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
