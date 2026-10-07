#include "net/room_rows.h"

#include "net/bridge_rows.h"

#include <algorithm>
#include <utility>

namespace {
net_motion motion_of(const stdb_motion& m) {
    net_motion motion;
    motion.zone = m.zone;
    motion.mode = static_cast<motion_mode>(std::min<std::uint8_t>(m.mode, static_cast<std::uint8_t>(motion_mode::swing)));
    motion.position = glm::vec3(m.x, m.y, m.z);
    motion.yaw = m.yaw;
    motion.speed = m.speed;
    motion.turn_rate = m.turn_rate;
    return motion;
}

room_shot shot_of(const stdb_shot_event& event) {
    room_shot shot;
    shot.account_id = event.account_id;
    shot.zone = event.zone;
    shot.stroke = event.shot.stroke;
    shot.input.ball_start = glm::vec3(event.start_x, event.start_y, event.start_z);
    shot.input.aim_angle = event.shot.aim_angle;
    shot.input.club_id = text_of(event.shot.club_id);
    shot.input.power = event.shot.power;
    shot.input.cigarette_active = event.shot.cigarette_active;
    shot.input.wind_time = event.shot.wind_time;
    shot.rest = glm::vec3(event.rest_x, event.rest_y, event.rest_z);
    shot.holed = event.holed;
    return shot;
}
}

void server_clock::sample(const std::int64_t server_time, const std::int64_t local_now) {
    const std::int64_t offset = server_time - local_now;
    offset_ = offset_ ? std::max(*offset_, offset) : offset;
}

std::int64_t server_clock::now(const std::int64_t local_now) const {
    return offset_ ? local_now + *offset_ : 0;
}

bool room_rows::apply(const stdb_event& event,
                      const std::uint64_t my_account,
                      const std::int64_t local_now,
                      server_clock& clock,
                      game_state& state) {
    const stdb_row& row = event.row;
    const stdb_row_change change = event.change;
    switch (event.table) {
    case STDB_TABLE_PLAYER:
        apply_change(names_, row.player.account_id, change, text_of(row.player.display_name));
        break;
    case STDB_TABLE_ROOM_MEMBER: {
        const stdb_room_member& m = row.room_member;
        apply_change(members_, m.account_id, change,
                     member{m.group_id, m.zone, m.hole_started_at_micros,
                            std::vector<int>(m.round_strokes, m.round_strokes + m.round_strokes_count)});
        break;
    }
    case STDB_TABLE_GOLF_GROUP:
        apply_change(group_sizes_, row.golf_group.group_id, change, row.golf_group.member_count);
        break;
    case STDB_TABLE_AVATAR_MOTION: {
        const stdb_avatar_motion& a = row.avatar_motion;
        if (change != STDB_ROW_DELETE && a.account_id == my_account) {
            clock.sample(a.server_time_micros, local_now);
        }
        apply_change(motions_, a.account_id, change, motion{motion_of(a.motion), a.server_time_micros});
        break;
    }
    case STDB_TABLE_BALL: {
        const stdb_ball& b = row.ball;
        apply_change(balls_, b.account_id, change, room_ball{b.zone, glm::vec3(b.x, b.y, b.z), b.stroke_count});
        break;
    }
    case STDB_TABLE_SHOT_EVENT:
        // Events only ever arrive as inserts.
        if (change != STDB_ROW_DELETE) {
            state.online.shots.push_back(shot_of(row.shot_event));
        }
        return true;
    case STDB_TABLE_EMOTE_EVENT: {
        const std::optional<emote_id> emote = emote_from_name(text_of(row.emote_event.emote_id));
        const std::uint64_t account = row.emote_event.account_id;
        if (change == STDB_ROW_DELETE || !emote) {
            return true;
        }
        if (account != my_account) {
            state.online.emotes.push_back(room_emote{account, *emote});
        } else if (*emote == emote_id::smoke) {
            state.online.smoke_accepted_at = clock.now(local_now);
        }
        return true;
    }
    default:
        return false;
    }
    changed_ = true;
    return true;
}

void room_rows::publish(game_state& state) {
    if (!changed_) {
        return;
    }
    changed_ = false;
    std::map<std::uint64_t, room_player>& players = state.online.players;
    players.clear();
    for (const auto& [account, m] : members_) {
        room_player player;
        const auto name = names_.find(account);
        if (name != names_.end()) {
            player.name = name->second;
        }
        player.group_id = m.group_id;
        player.zone = m.zone;
        player.hole_started_at = m.hole_started_at;
        player.round_strokes = m.round_strokes;
        const auto moved = motions_.find(account);
        if (moved != motions_.end()) {
            player.motion = moved->second.value;
            player.motion_at = moved->second.at;
        }
        players.emplace(account, std::move(player));
    }
    state.online.balls = balls_;
    state.online.group_sizes = group_sizes_;
}

void room_rows::clear(game_state& state) {
    members_.clear();
    names_.clear();
    motions_.clear();
    balls_.clear();
    group_sizes_.clear();
    changed_ = false;
    state.online.players.clear();
    state.online.balls.clear();
    state.online.group_sizes.clear();
}
