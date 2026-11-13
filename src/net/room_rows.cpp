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
    motion.client_time = m.client_time;
    motion.speed = m.speed;
    motion.turn_rate = m.turn_rate;
    motion.setup = sent_setup{m.setup.aim, m.setup.face_open, m.setup.ball_position, m.setup.backswing};
    motion.crouched = m.crouched;
    return motion;
}

loadout loadout_of(const stdb_player_loadout& row) {
    loadout equipped{text_of(row.cart), {}, text_of(row.smokable), text_of(row.drinkable), text_of(row.bag)};
    for (std::size_t i = 0; i < row.cart_mounts_count; ++i) {
        equipped.cart_mounts[text_of(row.cart_mounts[i].mount)] = text_of(row.cart_mounts[i].item_id);
    }
    return equipped;
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
    setup.strike_timing = event.shot.strike_timing;
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
    for (std::size_t i = 0; i < event.levels_count; ++i) {
        shot.golfer.levels[text_of(event.levels[i].skill_id)] = event.levels[i].level;
    }
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
    bag.place.cart_owner = row.cart_owner;
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
    std::uint64_t room = room_id_;
    switch (event.table) {
    case STDB_TABLE_ROOM_MEMBER: room = row.room_member.room_id; break;
    case STDB_TABLE_GOLF_GROUP: room = row.golf_group.room_id; break;
    case STDB_TABLE_AVATAR_MOTION: room = row.avatar_motion.room_id; break;
    case STDB_TABLE_DAP: room = row.dap.room_id; break;
    case STDB_TABLE_BALL: room = row.ball.room_id; break;
    case STDB_TABLE_GOLF_CART: room = row.golf_cart.room_id; break;
    case STDB_TABLE_PLAYER_BAG: room = row.player_bag.room_id; break;
    case STDB_TABLE_GREEN_FLAG: room = row.green_flag.room_id; break;
    case STDB_TABLE_SHOT_EVENT: room = row.shot_event.room_id; break;
    case STDB_TABLE_ITEM_USE_EVENT: room = row.item_use_event.room_id; break;
    default: break;
    }
    if (room_id_ == 0 || room != room_id_) {
        changed_ = true;
        return true;
    }
    switch (event.table) {
    case STDB_TABLE_PLAYER:
        apply_change(names_, row.player.account_id, change,
                     named{text_of(row.player.display_name), hand_of(row.player.hand), row.player.natural_shape,
                           row.player.favourite_colour});
        break;
    case STDB_TABLE_ROOM_MEMBER: {
        const stdb_room_member& m = row.room_member;
        apply_change(members_, m.account_id, change,
                     member{m.group_id, std::vector<int>(m.round_strokes, m.round_strokes + m.round_strokes_count), m.round_started});
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
        apply_view_change(motions_, a.account_id, change, motion{motion_of(a.motion), a.server_time_micros},
            [](const motion& left, const motion& right) {
                const net_motion& l = left.value;
                const net_motion& r = right.value;
                return left.at == right.at && l.mode == r.mode && l.position == r.position && l.yaw == r.yaw &&
                    l.speed == r.speed && l.turn_rate == r.turn_rate && l.client_time == r.client_time &&
                    l.setup == r.setup && l.crouched == r.crouched;
            });
        break;
    }
    case STDB_TABLE_RIDE_MOTION: {
        const stdb_ride_motion& r = row.ride_motion;
        if (r.rider != my_account) {
            return true;
        }
        if (change == STDB_ROW_DELETE) {
            ride_.reset();
        } else {
            ride_ = room_ride{r.owner, motion_of(r.motion), r.server_time_micros};
        }
        changed_ = true;
        return true;
    }
    case STDB_TABLE_BALL: {
        const stdb_ball& b = row.ball;
        if (b.account_id == my_account) { ++state.online.ball_updates; }
        apply_change(balls_, b.account_id, change, room_ball{b.hole, glm::vec3(b.x, b.y, b.z), b.stroke_count, b.in_cup, static_cast<ball_kind>(b.kind), b.tee_mat});
        break;
    }
    case STDB_TABLE_GOLF_CART: {
        const stdb_golf_cart& c = row.golf_cart;
        apply_view_change(carts_, c.owner, change,
                     room_cart{glm::vec3(c.x, c.y, c.z), c.yaw, c.passenger,
                               cart_condition_from_number(c.condition).value_or(cart_condition::upright), c.driver, c.motion_id},
            [](const room_cart& left, const room_cart& right) {
                return left.position == right.position && left.yaw == right.yaw && left.passenger == right.passenger &&
                    left.condition == right.condition && left.driver == right.driver && left.motion_id == right.motion_id;
            });
        break;
    }
    case STDB_TABLE_SHOT_EVENT:
        // Events only ever arrive as inserts.
        if (change != STDB_ROW_DELETE) {
            shots_.push_back(shot_of(row.shot_event));
        }
        changed_ = true;
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
    case STDB_TABLE_DAP: {
        const auto& d = row.dap;
        apply_change(daps_, d.id, change, room_dap{d.id, d.first, d.second, d.started_at,
            glm::vec3(d.first_x, d.first_y, d.first_z), glm::vec3(d.second_x, d.second_y, d.second_z)});
        break;
    }
    case STDB_TABLE_ITEM_USE_EVENT: {
        // Mine played as I used it.
        const std::uint64_t account = row.item_use_event.account_id;
        if (change != STDB_ROW_DELETE && account != my_account) {
            item_uses_.push_back(room_item_use{account, text_of(row.item_use_event.item_id)});
        }
        changed_ = true;
        return true;
    }
    default:
        return false;
    }
    changed_ = true;
    return true;
}

void room_rows::publish(game_state& state) {
    if (!ready_ || !changed_) {
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
            player.favourite_colour = name->second.favourite_colour;
        }
        player.group_id = m.group_id;
        player.round_started = m.round_started;
        player.round_strokes = m.round_strokes;
        const auto moved = motions_.find(account);
        if (moved != motions_.end()) {
            player.motion = moved->second.value;
            player.motion_at = moved->second.at;
        }
        players.emplace(account, std::move(player));
    }
    const auto ridden = ride_ ? carts_.find(ride_->owner) : carts_.end();
    state.online.ride = ridden != carts_.end() && ridden->second.passenger == state.online.account_id
        ? ride_ : std::nullopt;
    state.online.shots.insert(state.online.shots.end(), shots_.begin(), shots_.end());
    state.online.item_uses.insert(state.online.item_uses.end(), item_uses_.begin(), item_uses_.end());
    shots_.clear();
    item_uses_.clear();
    state.online.daps = daps_;
    state.online.balls = balls_;
    state.online.carts = carts_;
    state.online.loadouts = loadouts_;
    state.online.bags = bags_;
    for (auto it = state.online.loadouts.begin(); it != state.online.loadouts.end();) {
        it = members_.count(it->first) == 0 ? state.online.loadouts.erase(it) : std::next(it);
    }
    state.online.flags_out.clear();
    for (const auto& [id, hole] : flags_) {
        state.online.flags_out.push_back(hole);
    }
    state.online.group_sizes = group_sizes_;
}

void room_rows::clear(game_state& state) {
    room_id_ = 0;
    ready_ = false;
    ride_.reset();
    shots_.clear();
    item_uses_.clear();
    state.online.room_ready = false;
    state.online.shots.clear();
    state.online.item_uses.clear();
    state.online.ride.reset();
    members_.clear();
    names_.clear();
    motions_.clear();
    daps_.clear(); state.online.daps.clear(); state.dap_pending.reset();
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
    ride_.reset();
}

void room_rows::begin(const std::uint64_t room, game_state& state) {
    clear(state);
    room_id_ = room;
}

void room_rows::ready(game_state& state) {
    ready_ = room_id_ != 0;
    state.online.room_ready = ready_;
    changed_ = true;
    publish(state);
}
