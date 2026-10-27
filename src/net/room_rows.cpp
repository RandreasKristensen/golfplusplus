#include "net/room_rows.h"

#include "net/bridge_rows.h"

#include <algorithm>
#include <utility>

namespace {
net_motion motion_of(const stdb_motion& m) {
    net_motion motion;
    motion.mode = static_cast<motion_mode>(std::min<std::uint8_t>(m.mode, static_cast<std::uint8_t>(motion_mode::swing)));
    motion.position = glm::vec3(m.x, m.y, m.z);
    motion.yaw = m.yaw;
    motion.speed = m.speed;
    motion.turn_rate = m.turn_rate;
    return motion;
}

loadout loadout_of(const stdb_player_loadout& row) {
    loadout equipped{text_of(row.cart), {}, text_of(row.smokable), text_of(row.drinkable), text_of(row.bag)};
    for (std::size_t i = 0; i < row.cart_mounts_count; ++i) {
        equipped.cart_mounts[text_of(row.cart_mounts[i].mount)] = text_of(row.cart_mounts[i].item_id);
    }
    return equipped;
}

handedness hand_of(const std::uint8_t number) {
    return number == static_cast<std::uint8_t>(handedness::left) ? handedness::left : handedness::right;
}

room_shot shot_of(const stdb_shot_event& event) {
    room_shot shot;
    shot.account_id = event.account_id;
    shot.hole = event.hole;
    shot.stroke = event.shot.stroke;
    shot_setup& setup = shot.record.setup;
    setup.ball_start = glm::vec3(event.start_x, event.start_y, event.start_z);
    setup.aim_angle = event.shot.aim_angle;
    setup.face_open = event.shot.face_open;
    setup.ball_position = event.shot.ball_position;
    setup.backswing = event.shot.backswing;
    setup.club_id = text_of(event.shot.club_id);
    setup.hand = hand_of(event.hand);
    setup.shot_at = event.shot.shot_at;
    shot.record.seed = event.seed;
    for (std::size_t i = 0; i < event.snapshot_count; ++i) {
        const stdb_snapshot_entry& entry = event.snapshot[i];
        const snapshot_kind kind = entry.kind <= static_cast<std::uint8_t>(snapshot_kind::ball)
            ? static_cast<snapshot_kind>(entry.kind)
            : snapshot_kind::person;
        shot.record.snapshot.push_back(snapshot_entry{kind, glm::vec3(entry.x, entry.y, entry.z), entry.yaw, entry.shooter});
    }
    shot.golfer.hand = setup.hand;
    shot.golfer.natural = natural_shape_from_index(event.natural_shape).value_or(natural_shape::neutral);
    shot.golfer.spread_scale = event.spread_scale;
    shot.flags_out.assign(event.shot.flags_out, event.shot.flags_out + event.shot.flags_out_count);
    shot.rest = glm::vec3(event.rest_x, event.rest_y, event.rest_z);
    shot.holed = event.holed;
    return shot;
}

room_bag bag_of(const stdb_player_bag& row) {
    room_bag bag;
    bag.place.where = row.place <= static_cast<std::uint8_t>(bag_where::on_ground) ? static_cast<bag_where>(row.place)
                                                                                  : bag_where::on_back;
    bag.place.seat = row.seat == static_cast<std::uint8_t>(cart_seat::passenger) ? cart_seat::passenger : cart_seat::driver;
    bag.place.position = glm::vec3(row.x, row.y, row.z);
    bag.place.yaw = row.yaw;
    bag.club_in_hand = text_of(row.club_in_hand);
    return bag;
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
        apply_change(names_, row.player.account_id, change,
                     named{text_of(row.player.display_name), hand_of(row.player.hand), row.player.natural_shape});
        break;
    case STDB_TABLE_ROOM_MEMBER: {
        const stdb_room_member& m = row.room_member;
        apply_change(members_, m.account_id, change,
                     member{m.group_id, std::vector<int>(m.round_strokes, m.round_strokes + m.round_strokes_count)});
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
        apply_change(balls_, b.account_id, change, room_ball{b.hole, glm::vec3(b.x, b.y, b.z), b.stroke_count, b.in_cup});
        break;
    }
    case STDB_TABLE_GOLF_CART: {
        const stdb_golf_cart& c = row.golf_cart;
        apply_change(carts_, c.owner, change,
                     room_cart{glm::vec3(c.x, c.y, c.z), c.yaw, c.passenger,
                               cart_condition_from_number(c.condition).value_or(cart_condition::upright)});
        break;
    }
    case STDB_TABLE_SHOT_EVENT:
        // Events only ever arrive as inserts.
        if (change != STDB_ROW_DELETE) {
            state.online.shots.push_back(shot_of(row.shot_event));
        }
        return true;
    case STDB_TABLE_PLAYER_LOADOUT:
        apply_change(loadouts_, row.player_loadout.account_id, change, loadout_of(row.player_loadout));
        break;
    case STDB_TABLE_PLAYER_BAG:
        apply_change(bags_, row.player_bag.account_id, change, bag_of(row.player_bag));
        break;
    case STDB_TABLE_GREEN_FLAG:
        apply_change(flags_, row.green_flag.id, change, static_cast<int>(row.green_flag.hole));
        break;
    case STDB_TABLE_ITEM_USE_EVENT: {
        // Mine played as I used it.
        const std::uint64_t account = row.item_use_event.account_id;
        if (change != STDB_ROW_DELETE && account != my_account) {
            state.online.item_uses.push_back(room_item_use{account, text_of(row.item_use_event.item_id)});
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
            player.name = name->second.name;
            player.hand = name->second.hand;
            player.natural_shape = name->second.natural_shape;
        }
        player.group_id = m.group_id;
        player.round_strokes = m.round_strokes;
        const auto moved = motions_.find(account);
        if (moved != motions_.end()) {
            player.motion = moved->second.value;
            player.motion_at = moved->second.at;
        }
        players.emplace(account, std::move(player));
    }
    state.online.balls = balls_;
    state.online.carts = carts_;
    state.online.loadouts = loadouts_;
    state.online.bags = bags_;
    state.online.flags_out.clear();
    for (const auto& [id, hole] : flags_) {
        state.online.flags_out.push_back(hole);
    }
    state.online.group_sizes = group_sizes_;
}

void room_rows::clear(game_state& state) {
    members_.clear();
    names_.clear();
    motions_.clear();
    balls_.clear();
    carts_.clear();
    loadouts_.clear();
    bags_.clear();
    flags_.clear();
    group_sizes_.clear();
    changed_ = false;
    state.online.players.clear();
    state.online.balls.clear();
    state.online.carts.clear();
    state.online.loadouts.clear();
    state.online.bags.clear();
    state.online.flags_out.clear();
    state.online.group_sizes.clear();
}
