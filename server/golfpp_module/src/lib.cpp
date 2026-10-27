// The golf++ SpacetimeDB module: its tables, views and reducers.
// SpacetimeDB's macros register everything from one translation unit, so the
// whole schema and every reducer live here. Decisions are made by the plain
// rule files next to it (account_rules.h, server_content.h), which the
// native tests check; this file only reads and writes rows.
//
// Accounts are not logins: every login identity maps to one account
// (account_login), and all progress is keyed by account_id.

#include "account_rules.h"
#include "game/net_types.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/progression.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "module_cache.h"
#include "physics/vector_math.h"
#include "play_rules.h"
#include "game/server_errors.h"

#include <spacetimedb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

using namespace SpacetimeDB;

// =============================================================================
// Tables
// =============================================================================

// The server's settings: one row (id 0), written by init and by the owner.
struct server_config_row {
    std::uint32_t id;
    Identity owner;
    std::string auth_issuer;
    std::string auth_audience;
    bool allow_anonymous;
    std::string link_secret;  // keys link codes (make_link_code); empty: linking is off
};
SPACETIMEDB_STRUCT(server_config_row, id, owner, auth_issuer, auth_audience, allow_anonymous, link_secret)
SPACETIMEDB_TABLE(server_config_row, server_config, Private)
FIELD_PrimaryKey(server_config, id)

// An account. display_name is empty until claimed; name_key is its
// case-folded form, unique among named accounts. As a golfer (set_golfer):
// their hand (handedness) and natural shape (game/golfer.h natural_shape,
// natural_shape_unset until picked).
struct player_row {
    std::uint64_t account_id;
    std::string display_name;
    std::string name_key;
    bool online;
    std::int32_t holes_completed;  // the clock for collectible cooldowns, as in save_data
    Timestamp created_at;
    Timestamp last_login;
    std::uint8_t hand;
    std::uint8_t natural_shape;
};
SPACETIMEDB_STRUCT(player_row, account_id, display_name, name_key, online, holes_completed, created_at, last_login, hand,
                   natural_shape)
SPACETIMEDB_TABLE(player_row, player, Public)
FIELD_PrimaryKeyAutoInc(player, account_id)
FIELD_Index(player, name_key)

// One login identity of an account.
struct account_login_row {
    Identity identity;
    std::uint64_t account_id;
    std::string login_method;  // login_method_name
    Timestamp linked_at;
};
SPACETIMEDB_STRUCT(account_login_row, identity, account_id, login_method, linked_at)
SPACETIMEDB_TABLE(account_login_row, account_login, Private)
FIELD_PrimaryKey(account_login, identity)
FIELD_Index(account_login, account_id)

// The account's live connection. A newer connection of the same account
// replaces it, and the older one's reducer calls are refused from then on.
struct session_row {
    std::uint64_t account_id;
    Identity identity;
    ConnectionId connection_id;
};
SPACETIMEDB_STRUCT(session_row, account_id, identity, connection_id)
SPACETIMEDB_TABLE(session_row, session, Private)
FIELD_PrimaryKey(session, account_id)

// An account's one link code. Treat it like a password: only its owner sees
// it (my_link_code), and it is deleted when used or expired.
struct link_code_row {
    std::string code;
    std::uint64_t account_id;
    Timestamp expires_at;
};
SPACETIMEDB_STRUCT(link_code_row, code, account_id, expires_at)
SPACETIMEDB_TABLE(link_code_row, link_code, Private)
FIELD_PrimaryKey(link_code, code)
FIELD_Unique(link_code, account_id)

// Deletes a link code when it expires.
struct link_code_expiry_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
    std::string code;
};
SPACETIMEDB_STRUCT(link_code_expiry_row, scheduled_id, scheduled_at, code)
SPACETIMEDB_TABLE(link_code_expiry_row, link_code_expiry, Private)
FIELD_PrimaryKeyAutoInc(link_code_expiry, scheduled_id)
FIELD_Index(link_code_expiry, code)
SPACETIMEDB_SCHEDULE(link_code_expiry, 1, expire_link_code)

// Link codes an account made this hour (server.link_codes_per_hour).
struct link_budget_row {
    std::uint64_t account_id;
    std::int64_t window_start;  // microseconds since the epoch
    std::uint32_t count;
};
SPACETIMEDB_STRUCT(link_budget_row, account_id, window_start, count)
SPACETIMEDB_TABLE(link_budget_row, link_budget, Private)
FIELD_PrimaryKey(link_budget, account_id)

// A login's wrong link codes this hour (server.link_failures_per_hour), and
// how its last attempt went (my_link_status).
struct link_attempt_row {
    Identity identity;
    std::int64_t window_start;
    std::uint32_t failures;
    std::string last_result;  // link_result_linked, link_result_invalid_code, ...
    Timestamp last_at;
};
SPACETIMEDB_STRUCT(link_attempt_row, identity, window_start, failures, last_result, last_at)
SPACETIMEDB_TABLE(link_attempt_row, link_attempt, Private)
FIELD_PrimaryKey(link_attempt, identity)

// Progress, mirroring save_data so progress_rules runs on the server unchanged.
struct player_skill_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string skill_id;
    std::int32_t xp;
};
SPACETIMEDB_STRUCT(player_skill_row, id, account_id, skill_id, xp)
SPACETIMEDB_TABLE(player_skill_row, player_skill, Public)
FIELD_PrimaryKeyAutoInc(player_skill, id)
FIELD_Index(player_skill, account_id)

struct hole_score_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string course_id;
    std::int32_t hole_index;
    std::int32_t strokes;
    std::int32_t par;
    Timestamp at;
};
SPACETIMEDB_STRUCT(hole_score_row, id, account_id, course_id, hole_index, strokes, par, at)
SPACETIMEDB_TABLE(hole_score_row, hole_score, Public)
FIELD_PrimaryKeyAutoInc(hole_score, id)
FIELD_Index(hole_score, account_id)

struct completed_course_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string course_id;
    Timestamp at;
};
SPACETIMEDB_STRUCT(completed_course_row, id, account_id, course_id, at)
SPACETIMEDB_TABLE(completed_course_row, completed_course, Public)
FIELD_PrimaryKeyAutoInc(completed_course, id)
FIELD_Index(completed_course, account_id)

// A claimed collectible. Repeatable ones keep their claim count and the
// account's holes_completed at the last claim (-1: never).
struct collected_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string collectible_id;
    bool repeatable;
    std::int32_t claim_count;
    std::int32_t claimed_at_holes_completed;
};
SPACETIMEDB_STRUCT(collected_row, id, account_id, collectible_id, repeatable, claim_count, claimed_at_holes_completed)
SPACETIMEDB_TABLE(collected_row, collected, Public)
FIELD_PrimaryKeyAutoInc(collected, id)
FIELD_Index(collected, account_id)

struct world_flag_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string flag;
};
SPACETIMEDB_STRUCT(world_flag_row, id, account_id, flag)
SPACETIMEDB_TABLE(world_flag_row, world_flag, Public)
FIELD_PrimaryKeyAutoInc(world_flag, id)
FIELD_Index(world_flag, account_id)

// A course's room: up to server.room_capacity players walking and playing
// the same course together.
struct room_row {
    std::uint64_t room_id;
    std::string course_id;
    std::int32_t player_count;
    Timestamp created_at;
};
SPACETIMEDB_STRUCT(room_row, room_id, course_id, player_count, created_at)
SPACETIMEDB_TABLE(room_row, room, Public)
FIELD_PrimaryKeyAutoInc(room, room_id)
FIELD_Index(room, course_id)

// A player in a room. round_strokes is this round's score per hole (0: not
// played yet), which the group's scorecard shows.
struct room_member_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint64_t group_id;  // 0: none
    std::vector<std::int32_t> round_strokes;
};
SPACETIMEDB_STRUCT(room_member_row, account_id, room_id, group_id, round_strokes)
SPACETIMEDB_TABLE(room_member_row, room_member, Public)
FIELD_PrimaryKey(room_member, account_id)
FIELD_Index(room_member, room_id)

// Players who share a scorecard, up to server.group_capacity.
struct golf_group_row {
    std::uint64_t group_id;
    std::uint64_t room_id;
    std::uint64_t leader;
    std::int32_t member_count;
};
SPACETIMEDB_STRUCT(golf_group_row, group_id, room_id, leader, member_count)
SPACETIMEDB_TABLE(golf_group_row, golf_group, Public)
FIELD_PrimaryKeyAutoInc(golf_group, group_id)
FIELD_Index(golf_group, room_id)

// Where a player is and how they move (game/net_types.h net_motion), for
// others to carry on between updates.
struct avatar_motion_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint8_t mode;  // motion_mode
    float x;
    float y;
    float z;
    float yaw;
    float speed;
    float turn_rate;
    double client_time;  // the sender's clock, seconds
    Timestamp server_time;
};
SPACETIMEDB_STRUCT(avatar_motion_row, account_id, room_id, mode, x, y, z, yaw, speed, turn_rate, client_time, server_time)
SPACETIMEDB_TABLE(avatar_motion_row, avatar_motion, Public)
FIELD_PrimaryKey(avatar_motion, account_id)
FIELD_Index(avatar_motion, room_id)

// A player's ball, as game_state::ball offline: in play on its hole, or
// in_cup (the cup's centre on the ground, with the strokes it was holed in)
// until they pick it up (pick_up_ball). Deleted when picked up or given up,
// and when they leave the room, as leaving the course puts it away offline.
// rests_at is when the last shot's ball stops (server time): the next shot
// waits for it, as offline waits for the shot to play.
struct ball_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::int32_t hole;
    float x;
    float y;
    float z;
    std::int32_t stroke_count;
    std::int64_t last_shot_at;  // UTC milliseconds
    Timestamp rests_at;
    bool in_cup;
};
SPACETIMEDB_STRUCT(ball_row, account_id, room_id, hole, x, y, z, stroke_count, last_shot_at, rests_at, in_cup)
SPACETIMEDB_TABLE(ball_row, ball, Public)
FIELD_PrimaryKey(ball, account_id)
FIELD_Index(ball, room_id)

// A player's golf cart (game/golf_cart.h), there while they are in the
// room: parked beside hole 1 when they join (free_parking_slot), then where
// their last motion at its wheel left it (x, y, z the ground under the
// middle of its seat). `passenger` rides in its other seat (board_cart).
// `condition` is a cart_condition (physics/cart_dynamics.h): its driver
// says when it tips or sinks (overturn_cart), anyone beside it rights it
// (right_cart), and a sunk one is towed back (tow_cart).
struct golf_cart_row {
    std::uint64_t owner;
    std::uint64_t room_id;
    float x;
    float y;
    float z;
    float yaw;
    std::uint64_t passenger;  // 0: nobody
    std::uint8_t condition;
};
SPACETIMEDB_STRUCT(golf_cart_row, owner, room_id, x, y, z, yaw, passenger, condition)
SPACETIMEDB_TABLE(golf_cart_row, golf_cart, Public)
FIELD_PrimaryKey(golf_cart, owner)
FIELD_Index(golf_cart, room_id)

// Tows `owner`'s sunk cart back beside hole 1, its spec's tow_seconds after it sank.
struct cart_tow_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
    std::uint64_t owner;
};
SPACETIMEDB_STRUCT(cart_tow_row, scheduled_id, scheduled_at, owner)
SPACETIMEDB_TABLE(cart_tow_row, cart_tow, Private)
FIELD_PrimaryKeyAutoInc(cart_tow, scheduled_id)
SPACETIMEDB_SCHEDULE(cart_tow, 1, tow_cart)

// The server's view of a player's movement: the last accepted position and
// mode, the motion slack left (check_motion), and the movement XP meters not
// yet earned.
struct motion_budget_row {
    std::uint64_t account_id;
    float x;
    float y;
    float z;
    std::uint8_t mode;  // motion_mode
    Timestamp server_time;
    float slack_left;
    float walk_meters_pending;
    float cart_meters_pending;
};
SPACETIMEDB_STRUCT(motion_budget_row, account_id, x, y, z, mode, server_time, slack_left, walk_meters_pending,
                   cart_meters_pending)
SPACETIMEDB_TABLE(motion_budget_row, motion_budget, Private)
FIELD_PrimaryKey(motion_budget, account_id)

// What moved that a shot could meet, as it stood when it was hit
// (game/shot_snapshot.h snapshot_entry).
struct snapshot_row_entry {
    std::uint8_t kind;  // snapshot_kind
    float x;
    float y;
    float z;
    float yaw;
    bool shooter;
};
SPACETIMEDB_STRUCT(snapshot_row_entry, kind, x, y, z, yaw, shooter)

// A shot someone in the room hit, with everything needed to simulate it
// again (shot_record: the setup, the dice the server rolled for it and its
// snapshot, the greens' flags as they were) and where the server's
// simulation put the ball.
struct shot_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;
    std::int32_t hole;
    std::int32_t stroke;
    float start_x;
    float start_y;
    float start_z;
    float aim_angle;
    float face_open;
    float ball_position;
    float backswing;
    std::uint8_t hand;
    // The golfer as the server played them: their natural shape and the
    // spread their items gave them (game/golfer.h).
    std::uint8_t natural_shape;
    float spread_scale;
    std::string club_id;
    std::int64_t shot_at;  // UTC milliseconds: the weather it was played in
    std::uint64_t seed;
    std::vector<std::int32_t> flags_out;  // hole indices
    std::vector<snapshot_row_entry> snapshot;
    float rest_x;
    float rest_y;
    float rest_z;
    bool holed;
};
SPACETIMEDB_STRUCT(shot_event_row, room_id, account_id, hole, stroke, start_x, start_y, start_z, aim_angle, face_open,
                   ball_position, backswing, hand, natural_shape, spread_scale, club_id, shot_at, seed, flags_out, snapshot,
                   rest_x, rest_y, rest_z, holed)
SPACETIMEDB_TABLE(shot_event_row, shot_event, Public, true)
FIELD_Index(shot_event, room_id)

// Someone in the room used an item (game/item_catalog.h): everyone sees it.
struct item_use_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;
    std::string item_id;
};
SPACETIMEDB_STRUCT(item_use_event_row, room_id, account_id, item_id)
SPACETIMEDB_TABLE(item_use_event_row, item_use_event, Public, true)
FIELD_Index(item_use_event, room_id)

// An item at one of a cart's mounts (game/loadout.h's cart_mounts).
struct loadout_mount {
    std::string mount;
    std::string item_id;
};
SPACETIMEDB_STRUCT(loadout_mount, mount, item_id)

// What an account has equipped (game/loadout.h): the defaults until owning
// and choosing items exist. Others in the room see it: their cart and what
// they smoke.
struct player_loadout_row {
    std::uint64_t account_id;
    std::string cart;
    std::vector<loadout_mount> cart_mounts;
    std::string smokable;
    std::string drinkable;
    std::string bag;
};
SPACETIMEDB_STRUCT(player_loadout_row, account_id, cart, cart_mounts, smokable, drinkable, bag)
SPACETIMEDB_TABLE(player_loadout_row, player_loadout, Public)
FIELD_PrimaryKey(player_loadout, account_id)

// A player's bag in their room (game/bag_place.h) and the club in their
// hand: on their cart behind its driver's seat with the first club in hand
// when they join; moved by place_bag, a club taken from it by take_club.
struct player_bag_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint8_t place;  // bag_where
    std::uint8_t seat;   // cart_seat, on_cart
    float x;             // on_ground
    float y;
    float z;
    float yaw;
    std::string club_in_hand;
};
SPACETIMEDB_STRUCT(player_bag_row, account_id, room_id, place, seat, x, y, z, yaw, club_in_hand)
SPACETIMEDB_TABLE(player_bag_row, player_bag, Public)
FIELD_PrimaryKey(player_bag, account_id)
FIELD_Index(player_bag, room_id)

// A green whose flag is out in a room (set_flag): there while it is out,
// for everyone on that green, until someone puts it back.
struct green_flag_row {
    std::uint64_t id;
    std::uint64_t room_id;
    std::int32_t hole;
};
SPACETIMEDB_STRUCT(green_flag_row, id, room_id, hole)
SPACETIMEDB_TABLE(green_flag_row, green_flag, Public)
FIELD_PrimaryKeyAutoInc(green_flag, id)
FIELD_Index(green_flag, room_id)

// When an account last used an item, by the server's clock: a use cannot
// start again while it plays.
struct item_use_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string item_id;
    std::int64_t used_at_ms;  // UTC milliseconds
};
SPACETIMEDB_STRUCT(item_use_row, id, account_id, item_id, used_at_ms)
SPACETIMEDB_TABLE(item_use_row, item_use, Private)
FIELD_PrimaryKeyAutoInc(item_use, id)
FIELD_Index(item_use, account_id)

// A timed effect of an item an account used (game/effects.h's active_effect),
// running until ends_at_ms. The account's own client follows its rows.
struct running_effect_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::string item_id;
    std::string target;
    std::uint8_t bucket;    // effect_bucket
    float value;
    std::uint8_t duration;  // effect_duration
    float seconds;
    std::int64_t started_at_ms;  // UTC milliseconds
    std::int64_t ends_at_ms;
};
SPACETIMEDB_STRUCT(running_effect_row, id, account_id, item_id, target, bucket, value, duration, seconds, started_at_ms,
                   ends_at_ms)
SPACETIMEDB_TABLE(running_effect_row, running_effect, Public)
FIELD_PrimaryKeyAutoInc(running_effect, id)
FIELD_Index(running_effect, account_id)

// What my_link_status shows the caller.
struct link_status {
    std::string result;
    Timestamp at;
};
SPACETIMEDB_STRUCT(link_status, result, at)

// What my_link_code shows the caller.
struct link_code_view {
    std::string code;
    Timestamp expires_at;
};
SPACETIMEDB_STRUCT(link_code_view, code, expires_at)

// What server_protocol shows everyone. Never changes, so every game, old or
// new, can read it.
struct server_protocol_info {
    std::uint32_t version;
};
SPACETIMEDB_STRUCT(server_protocol_info, version)

// =============================================================================
// Helpers
// =============================================================================

namespace {
constexpr std::uint32_t config_id = 0;
constexpr std::int64_t micros_per_second = 1000000;
constexpr std::int64_t micros_per_hour = 3600 * micros_per_second;
constexpr std::int64_t micros_per_millisecond = 1000;

std::int64_t minutes_in_micros(const float minutes) {
    return static_cast<std::int64_t>(static_cast<double>(minutes) * 60.0 * static_cast<double>(micros_per_second));
}

ConnectionId caller_connection(const ReducerContext& ctx) {
    return ctx.connection_id.value_or(ConnectionId());
}

login_config config_rules(const server_config_row& config) {
    return login_config{config.auth_issuer, config.auth_audience, config.allow_anonymous};
}

// The caller's account when this call comes from its live session.
struct caller_result {
    std::optional<player_row> player;
    std::string error;
};

caller_result caller(ReducerContext& ctx) {
    // init only checks the content on the first publish; a republish with
    // broken content must not let reducers read through it.
    if (cached_content() == nullptr) {
        return {std::nullopt, error_server_unavailable};
    }
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    if (!login) {
        return {std::nullopt, error_not_signed_in};
    }
    const std::optional<session_row> live = ctx.db[session_account_id].find(login->account_id);
    if (!live || live->identity != ctx.sender() || !(live->connection_id == caller_connection(ctx))) {
        return {std::nullopt, error_signed_in_elsewhere};
    }
    std::optional<player_row> account = ctx.db[player_account_id].find(login->account_id);
    return {account, account ? std::string() : std::string(error_not_signed_in)};
}

bool has_progress(ReducerContext& ctx, const player_row& player) {
    const std::uint64_t id = player.account_id;
    const auto any = [](auto&& rows) { return rows.begin() != rows.end(); };
    return !player.display_name.empty() || player.holes_completed > 0 || any(ctx.db[player_skill_account_id].filter(id)) ||
        any(ctx.db[hole_score_account_id].filter(id)) || any(ctx.db[completed_course_account_id].filter(id)) ||
        any(ctx.db[collected_account_id].filter(id)) || any(ctx.db[world_flag_account_id].filter(id));
}


glm::vec3 position_of(const motion_budget_row& budget) {
    return glm::vec3(budget.x, budget.y, budget.z);
}

cart_pose pose_of(const golf_cart_row& cart) {
    return cart_pose{glm::vec3(cart.x, cart.y, cart.z), cart.yaw};
}

// Takes `account` out of any passenger seat in `room_id`. False when they had none.
bool leave_passenger_seat(ReducerContext& ctx, const std::uint64_t room_id, const std::uint64_t account) {
    bool left = false;
    for (golf_cart_row cart : ctx.db[golf_cart_room_id].filter(room_id)) {
        if (cart.passenger == account) {
            cart.passenger = 0;
            ctx.db[golf_cart_owner].update(cart);
            left = true;
        }
    }
    return left;
}

float seconds_between(const Timestamp& earlier, const Timestamp& later) {
    return static_cast<float>(static_cast<double>(later.micros_since_epoch() - earlier.micros_since_epoch()) /
                              static_cast<double>(micros_per_second));
}

// --- Progress: the account's rows as a save_data, so progress_rules runs on
// the server exactly as offline, and back.

save_data load_progress(ReducerContext& ctx, const std::uint64_t account_id) {
    save_data progress;
    if (const std::optional<player_row> account = ctx.db[player_account_id].find(account_id)) {
        progress.holes_completed = account->holes_completed;
    }
    for (const player_skill_row& skill : ctx.db[player_skill_account_id].filter(account_id)) {
        progress.skills[skill.skill_id].xp = skill.xp;
    }
    for (const completed_course_row& course : ctx.db[completed_course_account_id].filter(account_id)) {
        progress.completed_course_ids.push_back(course.course_id);
    }
    for (const collected_row& claim : ctx.db[collected_account_id].filter(account_id)) {
        if (!claim.repeatable) {
            progress.collected_ids.push_back(claim.collectible_id);
            continue;
        }
        repeatable_collectible_state& state = progress.repeatable_collectibles[claim.collectible_id];
        state.claim_count = claim.claim_count;
        if (claim.claimed_at_holes_completed >= 0) {
            state.claimed_at_holes_completed = claim.claimed_at_holes_completed;
        }
    }
    for (const world_flag_row& flag : ctx.db[world_flag_account_id].filter(account_id)) {
        progress.world_flags.push_back(flag.flag);
    }
    return progress;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

// Writes what changed between `before` and `after` (progress_rules only ever
// adds) to the account's rows.
void store_progress(ReducerContext& ctx, const std::uint64_t account_id, const save_data& before, const save_data& after) {
    if (after.holes_completed != before.holes_completed) {
        if (std::optional<player_row> account = ctx.db[player_account_id].find(account_id)) {
            account->holes_completed = after.holes_completed;
            ctx.db[player_account_id].update(*account);
        }
    }
    for (const auto& [skill_id, skill] : after.skills) {
        if (skill_xp(before.skills, skill_id) == skill.xp) {
            continue;
        }
        std::optional<player_skill_row> existing;
        for (const player_skill_row& row : ctx.db[player_skill_account_id].filter(account_id)) {
            if (row.skill_id == skill_id) {
                existing = row;
            }
        }
        if (existing) {
            existing->xp = skill.xp;
            ctx.db[player_skill_id].update(*existing);
        } else {
            ctx.db[player_skill].insert(player_skill_row{0, account_id, skill_id, skill.xp});
        }
    }
    for (const std::string& course_id : after.completed_course_ids) {
        if (!contains(before.completed_course_ids, course_id)) {
            ctx.db[completed_course].insert(completed_course_row{0, account_id, course_id, ctx.timestamp});
        }
    }
    for (const std::string& collectible_id : after.collected_ids) {
        if (!contains(before.collected_ids, collectible_id)) {
            ctx.db[collected].insert(collected_row{0, account_id, collectible_id, false, 1, -1});
        }
    }
    for (const auto& [collectible_id, state] : after.repeatable_collectibles) {
        const auto earlier = before.repeatable_collectibles.find(collectible_id);
        if (earlier != before.repeatable_collectibles.end() && earlier->second.claim_count == state.claim_count) {
            continue;
        }
        const collected_row row{0, account_id, collectible_id, true, state.claim_count,
                                state.claimed_at_holes_completed.value_or(-1)};
        std::optional<collected_row> existing;
        for (const collected_row& claim : ctx.db[collected_account_id].filter(account_id)) {
            if (claim.collectible_id == collectible_id) {
                existing = claim;
            }
        }
        if (existing) {
            collected_row updated = row;
            updated.id = existing->id;
            ctx.db[collected_id].update(updated);
        } else {
            ctx.db[collected].insert(row);
        }
    }
    for (const std::string& flag : after.world_flags) {
        if (!contains(before.world_flags, flag)) {
            ctx.db[world_flag].insert(world_flag_row{0, account_id, flag});
        }
    }
}

// --- Rooms

// Deletes `rows` (from an index) through their table's primary key `by_id`.
template <typename Rows, typename ById>
void delete_rows(Rows&& rows, ById&& by_id) {
    std::vector<std::uint64_t> ids;
    for (const auto& row : rows) {
        ids.push_back(row.id);
    }
    for (const std::uint64_t id : ids) {
        by_id.delete_by_key(id);
    }
}

const server_course* member_course(ReducerContext& ctx, const room_member_row& member) {
    const std::optional<room_row> joined = ctx.db[room_room_id].find(member.room_id);
    return joined ? cached_course(joined->course_id) : nullptr;
}

void leave_group_state(ReducerContext& ctx, room_member_row& member) {
    if (member.group_id == 0) {
        return;
    }
    std::optional<golf_group_row> group = ctx.db[golf_group_group_id].find(member.group_id);
    member.group_id = 0;
    ctx.db[room_member_account_id].update(member);
    if (!group) {
        return;
    }
    group->member_count -= 1;
    if (group->member_count <= 0) {
        ctx.db[golf_group_group_id].delete_by_key(group->group_id);
        return;
    }
    if (group->leader == member.account_id) {
        for (const room_member_row& other : ctx.db[room_member_room_id].filter(member.room_id)) {
            if (other.group_id == group->group_id) {
                group->leader = other.account_id;
                break;
            }
        }
    }
    ctx.db[golf_group_group_id].update(*group);
}

// Takes the account out of its room: group, avatar, ball (abandoning the
// hole, so no score) and membership; an empty room closes.
void leave_room_state(ReducerContext& ctx, const std::uint64_t account_id) {
    std::optional<room_member_row> member = ctx.db[room_member_account_id].find(account_id);
    if (!member) {
        return;
    }
    leave_group_state(ctx, *member);
    leave_passenger_seat(ctx, member->room_id, account_id);
    ctx.db[golf_cart_owner].delete_by_key(account_id);
    ctx.db[player_bag_account_id].delete_by_key(account_id);
    ctx.db[avatar_motion_account_id].delete_by_key(account_id);
    ctx.db[ball_account_id].delete_by_key(account_id);
    ctx.db[motion_budget_account_id].delete_by_key(account_id);
    ctx.db[room_member_account_id].delete_by_key(account_id);
    if (std::optional<room_row> joined = ctx.db[room_room_id].find(member->room_id)) {
        joined->player_count -= 1;
        if (joined->player_count <= 0) {
            delete_rows(ctx.db[green_flag_room_id].filter(joined->room_id), ctx.db[green_flag_id]);
            ctx.db[room_room_id].delete_by_key(joined->room_id);
        } else {
            ctx.db[room_room_id].update(*joined);
        }
    }
}

// Puts the player at `position` (course coordinates) at once, as the
// server's last accepted position and the avatar others see, standing: as
// teeing up stands them behind the ball offline. Nothing else of theirs
// changes (an item in use, its effects, the meters not yet earned).
void place_player(ReducerContext& ctx, const room_member_row& member, const glm::vec3& position) {
    if (std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(member.account_id)) {
        budget->x = position.x;
        budget->y = position.y;
        budget->z = position.z;
        budget->mode = static_cast<std::uint8_t>(motion_mode::idle);
        budget->server_time = ctx.timestamp;
        budget->slack_left = cached_content()->tuning.server.motion_distance_slack;
        ctx.db[motion_budget_account_id].update(*budget);
    }
    if (std::optional<avatar_motion_row> avatar = ctx.db[avatar_motion_account_id].find(member.account_id)) {
        avatar->mode = static_cast<std::uint8_t>(motion_mode::idle);
        avatar->x = position.x;
        avatar->y = position.y;
        avatar->z = position.z;
        avatar->speed = 0.0f;
        avatar->turn_rate = 0.0f;
        avatar->server_time = ctx.timestamp;
        ctx.db[avatar_motion_account_id].update(*avatar);
    }
}

// The member's round as round_state.h has it.
round_state current_round(const room_member_row& member) {
    round_state round = start_round(member.round_strokes.size());
    for (std::size_t i = 0; i < member.round_strokes.size(); ++i) {
        if (member.round_strokes[i] > 0) {
            round = complete_hole(round, i, member.round_strokes[i]);
        }
    }
    return round;
}

// The account's ball in play and its hole. A course republished with fewer
// holes may no longer have that hole: then the ball is given up (and the
// reducer returns Ok, so that is kept) instead of reading past the course's
// holes.
struct ball_on_hole {
    std::optional<ball_row> ball;
    bool given_up = false;
};

ball_on_hole ball_in_play_of(ReducerContext& ctx, const std::uint64_t account, const server_course* course) {
    const std::optional<ball_row> ball = ctx.db[ball_account_id].find(account);
    if (!ball || ball->in_cup || course == nullptr) {
        return {};
    }
    if (ball->hole >= 0 && static_cast<std::size_t>(ball->hole) < course->holes.size()) {
        return {ball, false};
    }
    ctx.db[ball_account_id].delete_by_key(account);
    return {std::nullopt, true};
}

// Whether the ball's last shot has played (by the server's clock, less the
// timing slack: the client starts playing it a little before the server hears
// of it).
bool shot_has_played(const ball_row& ball, const Timestamp& now, const server_tuning& tuning) {
    return seconds_between(now, ball.rests_at) <= tuning.timing_slack_seconds;
}

// The caller as a room member, with their account.
struct member_result {
    std::optional<player_row> player;
    std::optional<room_member_row> member;
    std::string error;
    bool sent_out = false;  // their room's course is gone: taken out of the room
};

// What a reducer returns when caller_member gave no member: Ok when the
// player was taken out of a room, so that stays done; else the error.
ReducerResult refused(const member_result& me) {
    return me.sent_out ? Ok() : Err(me.error);
}

member_result caller_member(ReducerContext& ctx) {
    const caller_result me = caller(ctx);
    if (!me.player) {
        return {std::nullopt, std::nullopt, me.error};
    }
    std::optional<room_member_row> member = ctx.db[room_member_account_id].find(me.player->account_id);
    if (!member) {
        return {me.player, std::nullopt, error_not_in_room, false};
    }
    // A republish may have removed their room's course (or broken it).
    if (member_course(ctx, *member) == nullptr) {
        leave_room_state(ctx, member->account_id);
        return {me.player, std::nullopt, error_not_in_room, true};
    }
    return {me.player, member, {}, false};
}

// Makes this connection the account's live session. An older session's room
// state ends; its client sees its room_member row go.
void start_session(ReducerContext& ctx, const std::uint64_t account_id) {
    const session_row row{account_id, ctx.sender(), caller_connection(ctx)};
    if (ctx.db[session_account_id].find(account_id)) {
        leave_room_state(ctx, account_id);
        ctx.db[session_account_id].update(row);
    } else {
        ctx.db[session].insert(row);
    }
    if (std::optional<player_row> player = ctx.db[player_account_id].find(account_id)) {
        player->online = true;
        player->last_login = ctx.timestamp;
        ctx.db[player_account_id].update(*player);
    }
}

// Deletes the account's link code, if any, with its expiry.
void delete_link_code(ReducerContext& ctx, const std::uint64_t account_id) {
    const std::optional<link_code_row> code = ctx.db[link_code_account_id].find(account_id);
    if (!code) {
        return;
    }
    std::vector<std::uint64_t> expiries;
    for (const link_code_expiry_row& expiry : ctx.db[link_code_expiry_code].filter(code->code)) {
        expiries.push_back(expiry.scheduled_id);
    }
    for (const std::uint64_t id : expiries) {
        ctx.db[link_code_expiry_scheduled_id].delete_by_key(id);
    }
    ctx.db[link_code_code].delete_by_key(code->code);
}

// --- Items: what an account has equipped, and the effects on it now.

loadout loadout_of(const player_loadout_row& row) {
    loadout equipped{row.cart, {}, row.smokable, row.drinkable, row.bag};
    for (const loadout_mount& mount : row.cart_mounts) {
        equipped.cart_mounts[mount.mount] = mount.item_id;
    }
    return equipped;
}

// The account's row, fitted to the items there are; the defaults with no row.
loadout load_loadout(ReducerContext& ctx, const std::uint64_t account_id) {
    const server_content& content = *cached_content();
    const std::optional<player_loadout_row> row = ctx.db[player_loadout_account_id].find(account_id);
    return row ? fitted_loadout(loadout_of(*row), content.default_loadout, content.items) : content.default_loadout;
}

// Gives the account the default loadout when it has none.
void ensure_loadout(ReducerContext& ctx, const std::uint64_t account_id) {
    if (ctx.db[player_loadout_account_id].find(account_id)) {
        return;
    }
    const loadout& defaults = cached_content()->default_loadout;
    std::vector<loadout_mount> mounts;
    for (const auto& [mount, item] : defaults.cart_mounts) {
        mounts.push_back(loadout_mount{mount, item});
    }
    ctx.db[player_loadout].insert(
        player_loadout_row{account_id, defaults.cart, mounts, defaults.smokable, defaults.drinkable, defaults.bag});
}

active_effect effect_of(const running_effect_row& row) {
    item_effect effect;
    effect.target = row.target;
    effect.bucket = static_cast<effect_bucket>(row.bucket);
    effect.value = row.value;
    effect.duration = static_cast<effect_duration>(row.duration);
    effect.seconds = row.seconds;
    return active_effect{row.item_id, effect, row.started_at_ms, row.ends_at_ms};
}

std::int64_t now_unix_ms(const ReducerContext& ctx) {
    return ctx.timestamp.micros_since_epoch() / micros_per_millisecond;
}

// Everything on the account now: what it equipped and what it used.
std::vector<active_effect> account_effects(ReducerContext& ctx, const std::uint64_t account_id) {
    std::vector<active_effect> effects = equipped_effects(load_loadout(ctx, account_id), cached_content()->items);
    for (const running_effect_row& row : ctx.db[running_effect_account_id].filter(account_id)) {
        effects.push_back(effect_of(row));
    }
    return effects;
}

xp_effects account_xp_effects(ReducerContext& ctx, const std::uint64_t account_id) {
    return xp_effects{account_effects(ctx, account_id), now_unix_ms(ctx)};
}

// The build of the cart the account has equipped (fitted, so a cart item).
const cart_spec& account_cart_spec(ReducerContext& ctx, const std::uint64_t account_id) {
    return *cached_content()->items.find(load_loadout(ctx, account_id).cart)->cart;
}

// The first free parking spot beside `course`'s hole 1 among the carts in `room_id`.
cart_pose free_parking(ReducerContext& ctx, const std::uint64_t room_id, const server_course& course) {
    const cart_tuning& carts = cached_content()->tuning.cart;
    std::vector<glm::vec3> parked;
    for (const golf_cart_row& other : ctx.db[golf_cart_room_id].filter(room_id)) {
        parked.push_back(glm::vec3(other.x, other.y, other.z));
    }
    const glm::vec3 start = course.world.hole_starts.front().position;
    const glm::vec3 pin = course.holes.front().pin_position;
    cart_pose cart = parked_cart_pose(start, pin, free_parking_slot(parked, start, pin, carts), carts);
    cart.position = anchor_on_terrain(course.area, cart.position);
    return cart;
}

// Deletes the account and everything keyed by it: its room state, session,
// logins, link rows and progress.
void delete_account(ReducerContext& ctx, const std::uint64_t account_id) {
    leave_room_state(ctx, account_id);
    ctx.db[session_account_id].delete_by_key(account_id);
    delete_link_code(ctx, account_id);
    ctx.db[link_budget_account_id].delete_by_key(account_id);
    std::vector<Identity> logins;
    for (const account_login_row& login : ctx.db[account_login_account_id].filter(account_id)) {
        logins.push_back(login.identity);
    }
    for (const Identity& identity : logins) {
        ctx.db[link_attempt_identity].delete_by_key(identity);
        ctx.db[account_login_identity].delete_by_key(identity);
    }
    delete_rows(ctx.db[player_skill_account_id].filter(account_id), ctx.db[player_skill_id]);
    delete_rows(ctx.db[hole_score_account_id].filter(account_id), ctx.db[hole_score_id]);
    delete_rows(ctx.db[completed_course_account_id].filter(account_id), ctx.db[completed_course_id]);
    delete_rows(ctx.db[collected_account_id].filter(account_id), ctx.db[collected_id]);
    delete_rows(ctx.db[world_flag_account_id].filter(account_id), ctx.db[world_flag_id]);
    delete_rows(ctx.db[item_use_account_id].filter(account_id), ctx.db[item_use_id]);
    delete_rows(ctx.db[running_effect_account_id].filter(account_id), ctx.db[running_effect_id]);
    ctx.db[player_loadout_account_id].delete_by_key(account_id);
    ctx.db[player_account_id].delete_by_key(account_id);
}

// --- Shots: the golfer, their bag and the room as a shot meets them.

bag_place bag_of(const player_bag_row& row) {
    bag_place bag;
    bag.where = static_cast<bag_where>(row.place);
    bag.seat = static_cast<cart_seat>(row.seat);
    bag.position = glm::vec3(row.x, row.y, row.z);
    bag.yaw = row.yaw;
    return bag;
}

// The account's own cart where its row has it; at the origin without one.
cart_pose own_cart_pose(ReducerContext& ctx, const std::uint64_t account_id) {
    const std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(account_id);
    return cart ? pose_of(*cart) : cart_pose{};
}

// The account as a golfer: its hand and natural shape from its row, the
// spread its items give it now.
golfer_profile golfer_of(ReducerContext& ctx, const player_row& player) {
    golfer_profile golfer;
    golfer.hand = player.hand == static_cast<std::uint8_t>(handedness::left) ? handedness::left : handedness::right;
    golfer.natural = natural_shape_from_index(player.natural_shape).value_or(natural_shape::neutral);
    golfer.spread_scale =
        effective_value(1.0f, effect_target_shot_spread, account_effects(ctx, player.account_id), now_unix_ms(ctx));
    return golfer;
}

// The holes whose flag is out in the room.
std::vector<int> room_flags_out(ReducerContext& ctx, const std::uint64_t room_id) {
    std::vector<int> out;
    for (const green_flag_row& flag : ctx.db[green_flag_room_id].filter(room_id)) {
        out.push_back(flag.hole);
    }
    return out;
}

// What moves in the room that `shooter`'s shot may meet, as the rows have it
// now: everyone on foot where their last motion left them (`shooter` among
// them), every cart not sunk, and every ball at rest but `shooter`'s.
std::vector<snapshot_entry> room_snapshot(ReducerContext& ctx, const std::uint64_t room_id, const std::uint64_t shooter) {
    std::vector<snapshot_entry> entries;
    for (const avatar_motion_row& avatar : ctx.db[avatar_motion_room_id].filter(room_id)) {
        if (!in_cart(static_cast<motion_mode>(avatar.mode))) {
            entries.push_back(snapshot_entry{snapshot_kind::person, glm::vec3(avatar.x, avatar.y, avatar.z), 0.0f,
                                             avatar.account_id == shooter});
        }
    }
    for (const golf_cart_row& cart : ctx.db[golf_cart_room_id].filter(room_id)) {
        if (cart.condition != static_cast<std::uint8_t>(cart_condition::sunk)) {
            entries.push_back(snapshot_entry{snapshot_kind::cart, glm::vec3(cart.x, cart.y, cart.z), cart.yaw, false});
        }
    }
    for (const ball_row& other : ctx.db[ball_room_id].filter(room_id)) {
        if (other.account_id != shooter && !other.in_cup) {
            entries.push_back(snapshot_entry{snapshot_kind::ball, glm::vec3(other.x, other.y, other.z), 0.0f, false});
        }
    }
    return entries;
}

std::vector<snapshot_row_entry> snapshot_rows(const std::vector<snapshot_entry>& entries) {
    std::vector<snapshot_row_entry> rows;
    for (const snapshot_entry& entry : entries) {
        rows.push_back(snapshot_row_entry{static_cast<std::uint8_t>(entry.kind), entry.position.x, entry.position.y,
                                          entry.position.z, entry.yaw, entry.shooter});
    }
    return rows;
}

// Whether the caller signs in as a guest (is_guest_login).
bool caller_is_guest(ReducerContext& ctx) {
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    return login && is_guest_login(login->login_method);
}

void record_link_attempt(ReducerContext& ctx, link_attempt_row attempt, const std::string& result) {
    attempt.last_result = result;
    attempt.last_at = ctx.timestamp;
    if (ctx.db[link_attempt_identity].find(attempt.identity)) {
        ctx.db[link_attempt_identity].update(attempt);
    } else {
        ctx.db[link_attempt].insert(attempt);
    }
}
}

// =============================================================================
// Lifecycle
// =============================================================================

// Writes the default config with the publisher as owner, and refuses to
// publish a module whose embedded content does not load (as the game refuses
// to start without its content). Real logins need admin_set_config.
SPACETIMEDB_INIT(init, ReducerContext ctx) {
    if (cached_content() == nullptr) {
        return Err("embedded content does not load: " + content_error());
    }
    ctx.db[server_config].insert(server_config_row{config_id, ctx.sender(), "", "", false, ""});
    return Ok();
}

// The JWT payload the host verified for the caller's connection, if any.
// Read here rather than through AuthCtx::get_jwt: the C++ bindings (2.10.2)
// take the host's "source exhausted" (-1), which ends every complete read,
// for an error, so get_jwt never returns a token.
std::optional<std::string> connection_jwt_payload(const ReducerContext& ctx) {
    if (!ctx.connection_id) {
        return std::nullopt;
    }
    std::array<std::uint8_t, 16> id{};  // little-endian, low half first
    for (int i = 0; i < 8; ++i) {
        id[i] = static_cast<std::uint8_t>(ctx.connection_id->id.low >> (i * 8));
        id[8 + i] = static_cast<std::uint8_t>(ctx.connection_id->id.high >> (i * 8));
    }
    SpacetimeDB::BytesSource source{};
    if (::get_jwt(id.data(), &source) != SpacetimeDB::Status(0) || source == SpacetimeDB::BytesSource{0}) {
        return std::nullopt;
    }
    std::string payload;
    std::array<std::uint8_t, 1024> chunk{};
    while (true) {
        std::size_t read = chunk.size();
        const std::int16_t result = ::bytes_source_read(source, chunk.data(), &read);
        payload.append(reinterpret_cast<const char*>(chunk.data()), read);
        if (result == -1) {
            return payload;  // exhausted: all of it read
        }
        if (result != 0) {
            return std::nullopt;
        }
    }
}

SPACETIMEDB_CLIENT_CONNECTED(client_connected, ReducerContext ctx) {
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    if (!config) {
        return Err(error_login_rejected);
    }
    const std::optional<std::string> payload = connection_jwt_payload(ctx);
    const login_claims claims = payload ? parse_login_claims(*payload) : login_claims{};
    const std::optional<login_method> method = check_login(claims, config_rules(*config), ctx.sender() == config->owner);
    if (!method) {
        // For whoever sets the server up: which issuer and audience came,
        // to compare with admin_set_config's (neither is secret).
        std::string audience;
        for (const std::string& each : claims.audience) {
            audience += (audience.empty() ? "" : ", ") + each;
        }
        LOG_WARN("login rejected: issuer \"" + claims.issuer + "\", audience [" + audience + "]");
        return Err(error_login_rejected);
    }

    std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    if (!login) {
        const player_row created =
            ctx.db[player].insert(player_row{0, "", "", false, 0, ctx.timestamp, ctx.timestamp,
                                             static_cast<std::uint8_t>(handedness::right), natural_shape_unset});
        login = account_login_row{ctx.sender(), created.account_id, login_method_name(*method), ctx.timestamp};
        ctx.db[account_login].insert(*login);
    }
    start_session(ctx, login->account_id);
    return Ok();
}

SPACETIMEDB_CLIENT_DISCONNECTED(client_disconnected, ReducerContext ctx) {
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    if (!login) {
        return Ok();
    }
    const std::optional<session_row> live = ctx.db[session_account_id].find(login->account_id);
    if (!live || live->identity != ctx.sender() || !(live->connection_id == caller_connection(ctx))) {
        return Ok();  // an older session ending after a newer one took over
    }
    if (is_guest_login(login->login_method)) {
        delete_account(ctx, login->account_id);
        return Ok();
    }
    leave_room_state(ctx, login->account_id);
    ctx.db[session_account_id].delete_by_key(login->account_id);
    if (std::optional<player_row> account = ctx.db[player_account_id].find(login->account_id)) {
        account->online = false;
        ctx.db[player_account_id].update(*account);
    }
    return Ok();
}

// =============================================================================
// Owner
// =============================================================================

SPACETIMEDB_REDUCER(admin_set_config,
                    ReducerContext ctx,
                    std::string auth_issuer,
                    std::string auth_audience,
                    bool allow_anonymous,
                    std::string link_secret) {
    std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    if (!config || ctx.sender() != config->owner) {
        return Err(error_owner_only);
    }
    if (!link_secret.empty() && link_secret.size() < min_link_secret_length) {
        return Err(error_invalid_config);
    }
    config->auth_issuer = auth_issuer;
    config->auth_audience = auth_audience;
    config->allow_anonymous = allow_anonymous;
    config->link_secret = link_secret;
    ctx.db[server_config_id].update(*config);
    return Ok();
}

// =============================================================================
// Names
// =============================================================================

SPACETIMEDB_REDUCER(claim_name, ReducerContext ctx, std::string name) {
    caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    if (!me.player->display_name.empty()) {
        return Err(error_has_name);
    }
    const server_content* content = cached_content();
    const name_check check = check_player_name(name, content->font, content->tuning.server);
    if (!check.error.empty()) {
        return Err(check.error);
    }
    for (const player_row& other : ctx.db[player_name_key].filter(check.key)) {
        (void)other;
        return Err(error_name_taken);
    }
    me.player->display_name = check.name;
    me.player->name_key = check.key;
    ctx.db[player_account_id].update(*me.player);
    return Ok();
}

// =============================================================================
// Linking another login to an account
// =============================================================================

SPACETIMEDB_REDUCER(create_link_code, ReducerContext ctx) {
    caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    if (caller_is_guest(ctx)) {
        return Err(error_guest_cannot_link);
    }
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    if (!config || config->link_secret.size() < min_link_secret_length) {
        return Err(error_linking_off);
    }
    const server_tuning& tuning = cached_content()->tuning.server;
    const std::uint64_t account = me.player->account_id;
    const std::optional<link_budget_row> budget = ctx.db[link_budget_account_id].find(account);
    const rate_window window = budget ? rate_window{budget->window_start, budget->count} : rate_window{};
    const std::optional<rate_window> used = use_rate_window(window, ctx.timestamp.micros_since_epoch(), micros_per_hour,
                                                            static_cast<std::uint32_t>(tuning.link_codes_per_hour));
    if (!used) {
        return Err(error_too_many_codes);
    }
    const link_budget_row next_budget{account, used->start_micros, used->count};
    if (budget) {
        ctx.db[link_budget_account_id].update(next_budget);
    } else {
        ctx.db[link_budget].insert(next_budget);
    }

    delete_link_code(ctx, account);
    const std::string nonce = std::to_string(ctx.timestamp.micros_since_epoch()) + ":" + std::to_string(account) + ":" +
        caller_connection(ctx).id.to_string() + ":";
    std::string code = make_link_code(config->link_secret, nonce + "0");
    for (int attempt = 1; ctx.db[link_code_code].find(code); ++attempt) {
        code = make_link_code(config->link_secret, nonce + std::to_string(attempt));
    }
    const Timestamp expires_at = ctx.timestamp + TimeDuration::from_micros(minutes_in_micros(tuning.link_code_minutes));
    ctx.db[link_code].insert(link_code_row{code, account, expires_at});
    ctx.db[link_code_expiry].insert(link_code_expiry_row{0, ScheduleAt::time(expires_at), code});
    return Ok();
}

SPACETIMEDB_REDUCER(expire_link_code, ReducerContext ctx, link_code_expiry_row expiry) {
    if (ctx.sender() != ctx.database_identity()) {
        return Err(error_internal_only);
    }
    const std::optional<link_code_row> code = ctx.db[link_code_code].find(expiry.code);
    if (code && code->expires_at <= ctx.timestamp) {
        ctx.db[link_code_code].delete_by_key(expiry.code);
    }
    return Ok();
}

// Wrong codes are not errors: the attempt must still count, and an error
// would roll it back. The outcome shows in my_link_status.
SPACETIMEDB_REDUCER(redeem_link_code, ReducerContext ctx, std::string typed_code) {
    caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    if (caller_is_guest(ctx)) {
        return Err(error_guest_cannot_link);
    }
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    if (!config || config->link_secret.size() < min_link_secret_length) {
        return Err(error_linking_off);
    }
    const server_tuning& tuning = cached_content()->tuning.server;
    const std::optional<link_attempt_row> earlier = ctx.db[link_attempt_identity].find(ctx.sender());
    link_attempt_row attempt = earlier.value_or(link_attempt_row{ctx.sender(), 0, 0, "", ctx.timestamp});

    const std::int64_t now = ctx.timestamp.micros_since_epoch();
    const std::uint32_t limit = static_cast<std::uint32_t>(tuning.link_failures_per_hour);
    const rate_window failures{attempt.window_start, attempt.failures};
    const std::optional<rate_window> may_fail = use_rate_window(failures, now, micros_per_hour, limit);
    if (!may_fail) {
        record_link_attempt(ctx, attempt, link_result_too_many_attempts);
        return Ok();
    }

    const std::optional<link_code_row> code = ctx.db[link_code_code].find(normalize_link_code(typed_code));
    if (!code || code->expires_at <= ctx.timestamp || !ctx.db[player_account_id].find(code->account_id)) {
        attempt.window_start = may_fail->start_micros;
        attempt.failures = may_fail->count;
        record_link_attempt(ctx, attempt, link_result_invalid_code);
        return Ok();
    }
    const std::uint64_t from = me.player->account_id;
    if (code->account_id == from) {
        record_link_attempt(ctx, attempt, link_result_same_account);
        return Ok();
    }
    if (has_progress(ctx, *me.player)) {
        record_link_attempt(ctx, attempt, link_result_has_progress);
        return Ok();
    }

    // Move this login to the code's account; drop the empty account it had
    // unless another login still uses it. The account's session moves here:
    // newest wins, so the code owner's other client, if still connected, is
    // signed in elsewhere from now on.
    std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    login->account_id = code->account_id;
    login->linked_at = ctx.timestamp;
    ctx.db[account_login_identity].update(*login);
    delete_link_code(ctx, code->account_id);
    leave_room_state(ctx, from);
    ctx.db[session_account_id].delete_by_key(from);
    auto others = ctx.db[account_login_account_id].filter(from);
    if (others.begin() == others.end()) {
        delete_account(ctx, from);
    } else {
        me.player->online = false;
        ctx.db[player_account_id].update(*me.player);
    }
    start_session(ctx, code->account_id);
    record_link_attempt(ctx, attempt, link_result_linked);
    return Ok();
}

// =============================================================================
// Rooms and groups
// =============================================================================

// Joins the fullest room of the course with space, or opens one, standing at
// hole 1's start with no ball out, as offline.
SPACETIMEDB_REDUCER(join_course, ReducerContext ctx, std::string course_id) {
    const caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    if (me.player->display_name.empty()) {
        return Err(error_needs_name);
    }
    const server_course* course = cached_course(course_id);
    if (course == nullptr) {
        return Err(error_unknown_course);
    }
    const std::uint64_t account = me.player->account_id;
    leave_room_state(ctx, account);
    ensure_loadout(ctx, account);

    std::vector<room_candidate> candidates;
    for (const room_row& open : ctx.db[room_course_id].filter(course_id)) {
        candidates.push_back(room_candidate{open.room_id, open.player_count});
    }
    const int capacity = cached_content()->tuning.server.room_capacity;
    std::optional<room_row> joined;
    if (const std::optional<std::uint64_t> chosen = choose_room(candidates, capacity)) {
        joined = ctx.db[room_room_id].find(*chosen);
    } else {
        joined = ctx.db[room].insert(room_row{0, course_id, 0, ctx.timestamp});
    }
    joined->player_count += 1;
    ctx.db[room_room_id].update(*joined);

    const glm::vec3 spawn = anchor_on_terrain(course->area, course->world.hole_starts.front().position);
    const std::vector<std::int32_t> unplayed(course->holes.size(), 0);
    ctx.db[room_member].insert(room_member_row{account, joined->room_id, 0, unplayed});
    ctx.db[motion_budget].insert(motion_budget_row{account, spawn.x, spawn.y, spawn.z,
                                                   static_cast<std::uint8_t>(motion_mode::idle), ctx.timestamp,
                                                   cached_content()->tuning.server.motion_distance_slack, 0.0f, 0.0f});
    ctx.db[avatar_motion].insert(avatar_motion_row{account, joined->room_id, static_cast<std::uint8_t>(motion_mode::idle),
                                                   spawn.x, spawn.y, spawn.z, 0.0f, 0.0f, 0.0f, 0.0, ctx.timestamp});

    // A cart of their own, parked beside hole 1 next to the others in the room.
    const cart_pose cart = free_parking(ctx, joined->room_id, *course);
    ctx.db[golf_cart].insert(golf_cart_row{account, joined->room_id, cart.position.x, cart.position.y, cart.position.z, cart.yaw, 0,
                                           static_cast<std::uint8_t>(cart_condition::upright)});
    // Their bag on it, the longest club in hand.
    ctx.db[player_bag].insert(player_bag_row{account, joined->room_id, static_cast<std::uint8_t>(bag_where::on_cart),
                                             static_cast<std::uint8_t>(cart_seat::driver), 0.0f, 0.0f, 0.0f, 0.0f,
                                             cached_content()->clubs.front().id});
    return Ok();
}

SPACETIMEDB_REDUCER(leave_room, ReducerContext ctx) {
    const caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    leave_room_state(ctx, me.player->account_id);
    return Ok();
}

SPACETIMEDB_REDUCER(create_group, ReducerContext ctx) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->group_id != 0) {
        return Err(error_in_group);
    }
    const golf_group_row group = ctx.db[golf_group].insert(golf_group_row{0, me.member->room_id, me.member->account_id, 1});
    me.member->group_id = group.group_id;
    ctx.db[room_member_account_id].update(*me.member);
    return Ok();
}

SPACETIMEDB_REDUCER(join_group, ReducerContext ctx, std::uint64_t group_id) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->group_id != 0) {
        return Err(error_in_group);
    }
    std::optional<golf_group_row> group = ctx.db[golf_group_group_id].find(group_id);
    if (!group || group->room_id != me.member->room_id) {
        return Err(error_unknown_group);
    }
    if (group->member_count >= cached_content()->tuning.server.group_capacity) {
        return Err(error_group_full);
    }
    group->member_count += 1;
    ctx.db[golf_group_group_id].update(*group);
    me.member->group_id = group_id;
    ctx.db[room_member_account_id].update(*me.member);
    return Ok();
}

SPACETIMEDB_REDUCER(leave_group, ReducerContext ctx) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->group_id == 0) {
        return Err(error_not_in_group);
    }
    leave_group_state(ctx, *me.member);
    return Ok();
}

// =============================================================================
// Playing
// =============================================================================

// The client moved. Positions are the client's; the server only checks they
// are possible and earns the movement XP, as offline. A refused move leaves
// avatar_motion at the last accepted one.
SPACETIMEDB_REDUCER(update_motion,
                    ReducerContext ctx,
                    std::uint8_t mode,
                    float x,
                    float y,
                    float z,
                    float yaw,
                    float speed,
                    float turn_rate,
                    double client_time) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(yaw) &&
        std::isfinite(speed) && std::isfinite(turn_rate) && std::isfinite(client_time);
    if (mode >= motion_mode_count || !finite) {
        return Err(error_invalid_motion);
    }
    std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    const server_course* course = member_course(ctx, *me.member);
    if (!budget || course == nullptr) {
        return Err(error_not_in_room);
    }
    const server_content& content = *cached_content();
    const glm::vec3 from = position_of(*budget);
    const glm::vec3 to(x, y, z);
    // The last accepted motion: where this stretch started and how it went.
    const std::optional<avatar_motion_row> last = ctx.db[avatar_motion_account_id].find(me.member->account_id);
    net_motion previous;
    previous.mode = static_cast<motion_mode>(budget->mode);
    previous.position = from;
    previous.yaw = last ? last->yaw : 0.0f;
    previous.speed = last ? last->speed : 0.0f;
    previous.turn_rate = last ? last->turn_rate : 0.0f;
    const movement_limits moving = movement_limits_for(content.tuning, account_cart_spec(ctx, me.member->account_id),
                                                       account_effects(ctx, me.member->account_id), now_unix_ms(ctx));
    motion_check move = check_motion(previous, to, seconds_between(budget->server_time, ctx.timestamp), mode,
                                     budget->slack_left, moving, content.tuning);
    // Addressing the ball steps the player around it at once (address
    // position), as offline does from beside a ball at rest: allowed from and
    // to within reach of their own ball once its shot has played. It earns
    // nothing.
    if (!move.allowed && at_ball(static_cast<motion_mode>(mode))) {
        const std::optional<ball_row> own_ball = ctx.db[ball_account_id].find(me.member->account_id);
        const float reach = content.tuning.player.ball_interact_radius;
        if (own_ball && !own_ball->in_cup && shot_has_played(*own_ball, ctx.timestamp, content.tuning.server)) {
            const glm::vec3 ball_at(own_ball->x, own_ball->y, own_ball->z);
            if (within_interact_reach(from, ball_at, reach, content.tuning.server) &&
                within_interact_reach(to, ball_at, reach, content.tuning.server)) {
                move = motion_check{true, budget->slack_left, 0.0f};
            }
        }
    }
    if (!move.allowed) {
        return Err(error_too_fast);
    }
    // Into the driver's seat from beside it; into the passenger's only by
    // board_cart.
    const bool driving = mode == static_cast<std::uint8_t>(motion_mode::cart);
    std::optional<golf_cart_row> own_cart = ctx.db[golf_cart_owner].find(me.member->account_id);
    if (driving && own_cart && own_cart->condition != static_cast<std::uint8_t>(cart_condition::upright)) {
        return Err(error_cart_overturned);
    }
    if (driving && budget->mode != mode) {
        if (!own_cart) {
            return Err(error_unknown_cart);
        }
        if (!within_seat_reach(pose_of(*own_cart), cart_seat::driver, to, content.tuning)) {
            return Err(error_too_far);
        }
    }
    if (mode == static_cast<std::uint8_t>(motion_mode::ride)) {
        bool riding = false;
        for (const golf_cart_row& cart : ctx.db[golf_cart_room_id].filter(me.member->room_id)) {
            riding = riding || cart.passenger == me.member->account_id;
        }
        if (!riding) {
            return Err(error_not_in_cart);
        }
    }

    // Movement XP as offline, for the stretch since the last update in the
    // mode it was covered in (an update is sent when the mode changes, so
    // the new mode starts here). Progress is only read when a rate has
    // gathered enough meters for XP.
    const float meters = move.earned_distance * content.tuning.scale.meters_per_world_unit;
    const bool on_road = on_cart_road(course->world.cart_roads, from, content.tuning.cart);
    const movement_earnings earns = movement_earns(budget->mode, on_road);
    const reward_rules& rewards = content.rewards;
    struct earning {
        bool earns;
        const movement_xp_rate* rate;
        float* pending;
    };
    const earning earnings[] = {{earns.walking, &rewards.walking, &budget->walk_meters_pending},
                                {earns.cart, &rewards.cart_on_road, &budget->cart_meters_pending}};
    const bool xp_due = std::any_of(std::begin(earnings), std::end(earnings), [meters](const earning& e) {
        return e.earns && *e.pending + meters >= e.rate->meters_per_xp;
    });
    if (xp_due) {
        const save_data before = load_progress(ctx, me.member->account_id);
        const xp_effects boosts = account_xp_effects(ctx, me.member->account_id);
        save_data after = before;
        for (const earning& e : earnings) {
            if (e.earns) {
                const movement_xp_update update = award_movement_xp(after, *e.rate, *e.pending, meters, boosts);
                after = update.update.progress;
                *e.pending = update.remainder_meters;
            }
        }
        store_progress(ctx, me.member->account_id, before, after);
    } else {
        for (const earning& e : earnings) {
            if (e.earns) {
                *e.pending += meters;
            }
        }
    }

    budget->x = x;
    budget->y = y;
    budget->z = z;
    budget->mode = mode;
    budget->slack_left = move.slack_left;
    budget->server_time = ctx.timestamp;
    ctx.db[motion_budget_account_id].update(*budget);
    // Others carry the avatar on with these: only what the game can produce.
    const float shown_speed = clamp_motion_speed(mode, speed, moving, content.tuning.server);
    const float shown_turn_rate = clamp_turn_rate(turn_rate, moving);
    ctx.db[avatar_motion_account_id].update(avatar_motion_row{me.member->account_id, me.member->room_id, mode, x, y, z, yaw,
                                                              shown_speed, shown_turn_rate, client_time, ctx.timestamp});
    // The cart goes where its driver takes it.
    if (driving && own_cart) {
        const cart_pose cart = cart_around_seat(to, yaw, cart_seat::driver, content.tuning.cart);
        own_cart->x = cart.position.x;
        own_cart->y = cart.position.y;
        own_cart->z = cart.position.z;
        own_cart->yaw = cart.yaw;
        ctx.db[golf_cart_owner].update(*own_cart);
    }
    return Ok();
}

// Into the passenger seat of `owner`'s cart in my room, from beside it, when
// nobody sits there.
SPACETIMEDB_REDUCER(board_cart, ReducerContext ctx, std::uint64_t owner) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(owner);
    if (!cart || cart->room_id != me.member->room_id || owner == me.member->account_id) {
        return Err(error_unknown_cart);
    }
    if (cart->passenger != 0) {
        return Err(error_seat_taken);
    }
    if (cart->condition != static_cast<std::uint8_t>(cart_condition::upright)) {
        return Err(error_cart_overturned);
    }
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget) {
        return Err(error_not_in_room);
    }
    if (!within_seat_reach(pose_of(*cart), cart_seat::passenger, position_of(*budget), cached_content()->tuning)) {
        return Err(error_too_far);
    }
    leave_passenger_seat(ctx, me.member->room_id, me.member->account_id);
    cart->passenger = me.member->account_id;
    ctx.db[golf_cart_owner].update(*cart);
    return Ok();
}

// Out of the passenger seat I sit in.
SPACETIMEDB_REDUCER(leave_cart, ReducerContext ctx) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (!leave_passenger_seat(ctx, me.member->room_id, me.member->account_id)) {
        return Err(error_not_in_cart);
    }
    return Ok();
}

// My cart went over under me where the server has it (my last motion at
// its wheel): onto a side, or sunk. Checked plausible there with how I was
// last driving (overturn_plausible); whoever rode in it is out of their
// seat, and a sunk cart is towed back after its tow_seconds.
SPACETIMEDB_REDUCER(overturn_cart, ReducerContext ctx, std::uint8_t condition) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const std::uint64_t account = me.member->account_id;
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(account);
    const server_course* course = member_course(ctx, *me.member);
    if (!cart || course == nullptr) {
        return Err(error_unknown_cart);
    }
    if (cart->condition != static_cast<std::uint8_t>(cart_condition::upright)) {
        return Err(error_cart_overturned);
    }
    const std::optional<cart_condition> to = cart_condition_from_number(condition);
    const server_content& content = *cached_content();
    const std::optional<avatar_motion_row> motion = ctx.db[avatar_motion_account_id].find(account);
    const bool was_driving = motion && motion->mode == static_cast<std::uint8_t>(motion_mode::cart);
    // The cart's physics runs cart.time_scale faster than the motion it sent.
    const float cart_time = content.tuning.cart.time_scale;
    const float speed = was_driving ? motion->speed * content.tuning.scale.meters_per_world_unit / cart_time : 0.0f;
    const float turn_rate = was_driving ? motion->turn_rate / cart_time : 0.0f;
    const cart_spec& spec = account_cart_spec(ctx, account);
    // Wetness changes neither the slope nor the water.
    const cart_ground ground =
        cart_ground_at(course->area, course->world.cart_roads, content.tuning, 0.0f, pose_of(*cart).position);
    if (!to || !overturn_plausible(*to, ground, cart->yaw, speed, turn_rate, spec, content.tuning.server)) {
        return Err(error_cart_steady);
    }
    cart->condition = condition;
    cart->passenger = 0;
    ctx.db[golf_cart_owner].update(*cart);
    if (*to == cart_condition::sunk) {
        const std::int64_t tow_micros =
            static_cast<std::int64_t>(static_cast<double>(spec.tow_seconds) * static_cast<double>(micros_per_second));
        const Timestamp towed_at = ctx.timestamp + TimeDuration::from_micros(tow_micros);
        ctx.db[cart_tow].insert(cart_tow_row{0, ScheduleAt::time(towed_at), account});
    }
    return Ok();
}

// Stands `owner`'s tipped cart in my room back on its wheels, from beside it.
SPACETIMEDB_REDUCER(right_cart, ReducerContext ctx, std::uint64_t owner) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(owner);
    if (!cart || cart->room_id != me.member->room_id) {
        return Err(error_unknown_cart);
    }
    const std::optional<cart_condition> lying = cart_condition_from_number(cart->condition);
    if (!lying || !cart_tipped(*lying)) {
        return Err(error_cart_upright);
    }
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget) {
        return Err(error_not_in_room);
    }
    const game_tuning& tuning = cached_content()->tuning;
    if (!within_interact_reach(position_of(*budget), pose_of(*cart).position, tuning.cart.board_reach, tuning.server)) {
        return Err(error_too_far);
    }
    cart->condition = static_cast<std::uint8_t>(cart_condition::upright);
    ctx.db[golf_cart_owner].update(*cart);
    return Ok();
}

// A sunk cart back on its wheels in a free spot beside hole 1.
SPACETIMEDB_REDUCER(tow_cart, ReducerContext ctx, cart_tow_row tow) {
    if (ctx.sender() != ctx.database_identity()) {
        return Err(error_internal_only);
    }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(tow.owner);
    const std::optional<room_member_row> member = ctx.db[room_member_account_id].find(tow.owner);
    const server_course* course = member ? member_course(ctx, *member) : nullptr;
    if (!cart || course == nullptr || cart->condition != static_cast<std::uint8_t>(cart_condition::sunk)) {
        return Ok();
    }
    const cart_pose parked = free_parking(ctx, cart->room_id, *course);
    cart->x = parked.position.x;
    cart->y = parked.position.y;
    cart->z = parked.position.z;
    cart->yaw = parked.yaw;
    cart->condition = static_cast<std::uint8_t>(cart_condition::upright);
    ctx.db[golf_cart_owner].update(*cart);
    return Ok();
}

// Tees the ball up on `hole_index` from that hole's start, the player behind
// it, as tee_up offline: not while the ball is in its cup (pick_up_ball
// first), nor while it is in play on another hole unless `give_up` gives
// that hole up (no score).
SPACETIMEDB_REDUCER(tee_up, ReducerContext ctx, std::int32_t hole_index, bool give_up) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const server_course* course = member_course(ctx, *me.member);
    if (course == nullptr || hole_index < 0 || static_cast<std::size_t>(hole_index) >= course->holes.size()) {
        return Err(error_unknown_hole);
    }
    const std::size_t index = static_cast<std::size_t>(hole_index);
    // A republished course may have another hole count than this round.
    me.member->round_strokes.resize(course->holes.size(), 0);
    if (hole_played(current_round(*me.member), index)) {
        return Err(error_hole_played);
    }
    const std::uint64_t account = me.member->account_id;
    const std::optional<ball_row> own = ctx.db[ball_account_id].find(account);
    if (own && own->in_cup) {
        return Err(error_ball_in_cup);
    }
    if (own && own->hole == hole_index) {
        return Err(error_ball_in_play);  // that hole's ball is played where it lies
    }
    if (own && !give_up) {
        return Err(error_ball_in_play);
    }
    std::map<std::uint64_t, room_ball> room_balls;
    for (const ball_row& other : ctx.db[ball_room_id].filter(me.member->room_id)) {
        room_balls[other.account_id] =
            room_ball{other.hole, glm::vec3(other.x, other.y, other.z), other.stroke_count, other.in_cup};
    }
    if (tee_in_use(room_balls, account, hole_index)) {
        return Err(error_tee_in_use);
    }
    const course_world_hole_start& start = course->world.hole_starts[index];
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(account);
    if (!budget || !within_interact_reach(position_of(*budget), start.position, start.interaction_radius,
                                          cached_content()->tuning.server)) {
        return Err(error_too_far);
    }

    ctx.db[room_member_account_id].update(*me.member);
    const server_content& content = *cached_content();
    const float radius = content.tuning.scale.ball_physics_radius_meters;
    const glm::vec3 tee = resting_ball_position(course->area, course->holes[index].tee_position, radius);
    const ball_row teed{account, me.member->room_id, hole_index, tee.x, tee.y, tee.z, 0, 0, ctx.timestamp, false};
    if (own) {
        ctx.db[ball_account_id].update(teed);
    } else {
        ctx.db[ball].insert(teed);
    }
    place_player(ctx, *me.member,
                 tee_stance_position(course->area, tee, course->area.cups[index].centre, content.tuning.player.ball_stand_off_distance));
    return Ok();
}

// Gives up the hole the ball is in play on: no score, the ball back in the
// pocket, the player where they stand.
SPACETIMEDB_REDUCER(give_up_hole, ReducerContext ctx) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const ball_on_hole on = ball_in_play_of(ctx, me.member->account_id, member_course(ctx, *me.member));
    if (!on.ball) {
        return on.given_up ? Ok() : Err(error_not_on_hole);
    }
    ctx.db[ball_account_id].delete_by_key(me.member->account_id);
    return Ok();
}

// Plays the setup the client sent from the server's own ball: checked
// (check_shot), its dice rolled here, the room's people, carts and balls at
// rest as they stand now (its snapshot), and the flags as the room has them.
// Every client simulates it again from its row, bit for bit; the server's
// decides where the ball rests, the stroke, and a holed ball's score and
// completions, as complete_current_hole does offline.
SPACETIMEDB_REDUCER(take_shot,
                    ReducerContext ctx,
                    std::int32_t stroke,
                    float aim_angle,
                    float face_open,
                    float ball_position,
                    float backswing,
                    std::string club_id,
                    std::int64_t shot_at,
                    std::vector<std::int32_t> flags_out) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const server_course* course = member_course(ctx, *me.member);
    ball_on_hole on = ball_in_play_of(ctx, me.member->account_id, course);
    if (!on.ball) {
        return on.given_up ? Ok() : Err(error_not_on_hole);
    }
    std::optional<ball_row>& ball_now = on.ball;
    const server_content& content = *cached_content();
    const std::uint64_t account = me.member->account_id;
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(account);
    const glm::vec3 player_position = budget ? position_of(*budget) : glm::vec3(ball_now->x, ball_now->y, ball_now->z);
    if (!shot_has_played(*ball_now, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    const std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(account);
    const server_golfer golfer{golfer_of(ctx, *me.player), bag ? bag->club_in_hand : std::string(),
                               bag && bag->place == static_cast<std::uint8_t>(bag_where::on_back),
                               room_flags_out(ctx, me.member->room_id)};
    const glm::vec3 from(ball_now->x, ball_now->y, ball_now->z);
    const shot_check check =
        check_shot(shot_request{stroke, aim_angle, face_open, ball_position, backswing, club_id, shot_at,
                                std::vector<int>(flags_out.begin(), flags_out.end())},
                   server_ball{from, ball_now->stroke_count, ball_now->last_shot_at, lie_at(course->area, from)},
                   player_position, golfer, now_unix_ms(ctx), content);
    if (!check.input) {
        return Err(check.error);
    }
    const std::size_t hole = static_cast<std::size_t>(ball_now->hole);
    const shot_setup& input = *check.input;
    const shot_record record{input, server_shot_seed(ctx.timestamp.micros_since_epoch(), account, stroke),
                             room_snapshot(ctx, me.member->room_id, account)};
    const shot_result result =
        simulate_struck_shot(record, golfer.golfer, server_shot_course(*course, hole, input.shot_at, golfer.flags_out, content),
                             content.tuning, content.clubs);

    const save_data before = load_progress(ctx, account);
    save_data after = award_xp(before, content.rewards.shot, account_xp_effects(ctx, account)).progress;
    ctx.db[shot_event].insert(shot_event_row{
        me.member->room_id, account, ball_now->hole, stroke, input.ball_start.x, input.ball_start.y, input.ball_start.z,
        input.aim_angle, input.face_open, input.ball_position, input.backswing, static_cast<std::uint8_t>(input.hand),
        static_cast<std::uint8_t>(natural_shape_index(golfer.golfer.natural)), golfer.golfer.spread_scale, input.club_id,
        input.shot_at, record.seed,
        std::vector<std::int32_t>(golfer.flags_out.begin(), golfer.flags_out.end()), snapshot_rows(record.snapshot),
        result.rest_position.x, result.rest_position.y, result.rest_position.z, result.holed});

    // Lost in water: back, a stroke later; a ball hit on a green: two.
    const std::int32_t strokes = stroke + result.penalty_strokes;
    if (!result.holed) {
        ball_now->x = result.rest_position.x;
        ball_now->y = result.rest_position.y;
        ball_now->z = result.rest_position.z;
        ball_now->stroke_count = strokes;
        ball_now->last_shot_at = input.shot_at;
        ball_now->rests_at = ctx.timestamp + TimeDuration::from_micros(static_cast<std::int64_t>(
                                                 static_cast<double>(result.duration) * static_cast<double>(micros_per_second)));
        ctx.db[ball_account_id].update(*ball_now);
        store_progress(ctx, account, before, after);
        return Ok();
    }

    ctx.db[hole_score].insert(hole_score_row{0, account, course->course.id, ball_now->hole, strokes,
                                             course->holes[hole].par, ctx.timestamp});
    after = apply_hole_completed(after);
    me.member->round_strokes.resize(course->holes.size(), 0);
    me.member->round_strokes[hole] = strokes;
    if (round_finished(current_round(*me.member))) {
        // Rooms are courses with a world, never practice holes, so the round
        // always completes the course. A new round starts.
        after = apply_course_completed(after, course->course.id);
        std::fill(me.member->round_strokes.begin(), me.member->round_strokes.end(), 0);
    }
    store_progress(ctx, account, before, after);
    ctx.db[room_member_account_id].update(*me.member);
    // The ball stays in the cup until it is picked up, as offline; the player
    // stays where they holed out from.
    const glm::vec3 cup = course->area.cups[hole].centre;
    ball_now->x = cup.x;
    ball_now->y = cup.y;
    ball_now->z = cup.z;
    ball_now->stroke_count = strokes;
    ball_now->last_shot_at = input.shot_at;
    ball_now->in_cup = true;
    ctx.db[ball_account_id].update(*ball_now);
    return Ok();
}

// Takes the flag of hole `hole`'s green out (`out`) or puts it back, for
// everyone in the room: from within player.flag_reach of its cup, as P offline.
SPACETIMEDB_REDUCER(set_flag, ReducerContext ctx, std::int32_t hole, bool out) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const server_course* course = member_course(ctx, *me.member);
    if (course == nullptr || hole < 0 || static_cast<std::size_t>(hole) >= course->area.cups.size()) {
        return Err(error_unknown_hole);
    }
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    const game_tuning& tuning = cached_content()->tuning;
    if (!budget || !within_interact_reach(position_of(*budget), course->area.cups[static_cast<std::size_t>(hole)].centre,
                                          tuning.player.flag_reach, tuning.server)) {
        return Err(error_too_far);
    }
    std::optional<green_flag_row> taken_out;
    for (const green_flag_row& flag : ctx.db[green_flag_room_id].filter(me.member->room_id)) {
        if (flag.hole == hole) {
            taken_out = flag;
        }
    }
    if (out && !taken_out) {
        ctx.db[green_flag].insert(green_flag_row{0, me.member->room_id, hole});
    } else if (!out && taken_out) {
        ctx.db[green_flag_id].delete_by_key(taken_out->id);
    }
    return Ok();
}

// Moves my bag (check_bag_move): `place` a bag_where, `seat` a cart_seat
// (onto my cart), the rest where it is set down.
SPACETIMEDB_REDUCER(place_bag, ReducerContext ctx, std::uint8_t place, std::uint8_t seat, float x, float y, float z, float yaw) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(me.member->account_id);
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!bag || !budget) {
        return Err(error_not_in_room);
    }
    if (place > static_cast<std::uint8_t>(bag_where::on_ground) || seat > static_cast<std::uint8_t>(cart_seat::passenger)) {
        return Err(error_invalid_motion);
    }
    const bag_place to{static_cast<bag_where>(place), static_cast<cart_seat>(seat), glm::vec3(x, y, z), yaw};
    const std::string error = check_bag_move(bag_of(*bag), to, position_of(*budget),
                                             own_cart_pose(ctx, me.member->account_id), cached_content()->tuning);
    if (!error.empty()) {
        return Err(error);
    }
    bag->place = place;
    bag->seat = seat;
    bag->x = x;
    bag->y = y;
    bag->z = z;
    bag->yaw = yaw;
    ctx.db[player_bag_account_id].update(*bag);
    return Ok();
}

// Takes `club_id` from my bag into my hand (check_take_club), putting back
// the one I held.
SPACETIMEDB_REDUCER(take_club, ReducerContext ctx, std::string club_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(me.member->account_id);
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!bag || !budget) {
        return Err(error_not_in_room);
    }
    const std::string error = check_take_club(club_id, bag_of(*bag), position_of(*budget),
                                              own_cart_pose(ctx, me.member->account_id), *cached_content());
    if (!error.empty()) {
        return Err(error);
    }
    bag->club_in_hand = club_id;
    ctx.db[player_bag_account_id].update(*bag);
    return Ok();
}

// My hand and natural shape as a golfer (game/golfer.h), as my settings have them.
SPACETIMEDB_REDUCER(set_golfer, ReducerContext ctx, std::uint8_t hand, std::uint8_t natural) {
    caller_result me = caller(ctx);
    if (!me.player) {
        return Err(me.error);
    }
    if (hand > static_cast<std::uint8_t>(handedness::left) ||
        (!natural_shape_from_index(natural) && natural != natural_shape_unset)) {
        return Err(error_invalid_config);
    }
    me.player->hand = hand;
    me.player->natural_shape = natural;
    ctx.db[player_account_id].update(*me.player);
    return Ok();
}

// Picks the holed ball out of its cup, in reach of the last accepted position
// (player.ball_interact_radius), as pick_up_cup_ball offline. Ok with no
// ball out: nothing is left to pick up.
SPACETIMEDB_REDUCER(pick_up_ball, ReducerContext ctx) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const std::optional<ball_row> in_cup = ctx.db[ball_account_id].find(me.member->account_id);
    if (!in_cup) {
        return Ok();
    }
    if (!in_cup->in_cup) {
        return Err(error_ball_in_play);
    }
    const server_content& content = *cached_content();
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget || !within_interact_reach(position_of(*budget), glm::vec3(in_cup->x, in_cup->y, in_cup->z),
                                          content.tuning.player.ball_interact_radius, content.tuning.server)) {
        return Err(error_too_far);
    }
    ctx.db[ball_account_id].delete_by_key(me.member->account_id);
    return Ok();
}

// Uses an item the caller has equipped (what they smoke or drink), as
// offline: its XP per use and its timed effects, by the server's clock; not
// again while a use plays, nor while the caller's shot plays. Everyone in the
// room sees it.
SPACETIMEDB_REDUCER(use_item, ReducerContext ctx, std::string item_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const std::uint64_t account = me.member->account_id;
    const server_content& content = *cached_content();
    const std::optional<ball_row> own_ball = ctx.db[ball_account_id].find(account);
    if (own_ball && !shot_has_played(*own_ball, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    const std::int64_t now = now_unix_ms(ctx);
    std::optional<item_use_row> last;
    for (const item_use_row& use : ctx.db[item_use_account_id].filter(account)) {
        if (use.item_id == item_id) {
            last = use;
        }
    }
    const std::string error = check_item_use(item_id, load_loadout(ctx, account), last ? last->used_at_ms : 0, now, content);
    if (!error.empty()) {
        return Err(error);
    }
    if (last) {
        last->used_at_ms = now;
        ctx.db[item_use_id].update(*last);
    } else {
        ctx.db[item_use].insert(item_use_row{0, account, item_id, now});
    }

    const item_definition& item = *content.items.find(item_id);
    if (item.use->xp_per_use) {
        const save_data before = load_progress(ctx, account);
        store_progress(ctx, account, before, award_xp(before, *item.use->xp_per_use, account_xp_effects(ctx, account)).progress);
    }
    // The timed effects as effects_after_use leaves them, ended ones dropped.
    std::vector<active_effect> running;
    std::vector<std::uint64_t> old_rows;
    for (const running_effect_row& row : ctx.db[running_effect_account_id].filter(account)) {
        running.push_back(effect_of(row));
        old_rows.push_back(row.id);
    }
    for (const std::uint64_t id : old_rows) {
        ctx.db[running_effect_id].delete_by_key(id);
    }
    for (const active_effect& effect : effects_after_use(running_effects(running, now), item, now)) {
        ctx.db[running_effect].insert(running_effect_row{0, account, effect.item, effect.effect.target,
                                                       static_cast<std::uint8_t>(effect.effect.bucket), effect.effect.value,
                                                       static_cast<std::uint8_t>(effect.effect.duration), effect.effect.seconds,
                                                       effect.started_at_ms, effect.ends_at_ms});
    }
    ctx.db[item_use_event].insert(item_use_event_row{me.member->room_id, account, item_id});
    return Ok();
}

// Claims a collectible in reach of the last accepted position, with the same
// claim_collectible rule as offline. Named apart in C++ so it does not
// overload that rule; clients call it claim_collectible.
SPACETIMEDB_REDUCER_NAMED(claim_collectible_reducer, "claim_collectible", ReducerContext ctx, std::string collectible_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const server_course* course = member_course(ctx, *me.member);
    if (course == nullptr) {
        return Err(error_not_in_room);
    }
    const auto& collectibles = course->world.collectibles;
    const auto found = std::find_if(collectibles.begin(), collectibles.end(),
                                    [&collectible_id](const course_world_collectible& c) { return c.id == collectible_id; });
    if (found == collectibles.end()) {
        return Err(error_unknown_collectible);
    }
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget || !within_interact_reach(position_of(*budget), found->position, found->interaction_radius,
                                          cached_content()->tuning.server)) {
        return Err(error_too_far);
    }
    const save_data before = load_progress(ctx, me.member->account_id);
    const claim_update claim = claim_collectible(before, *found, account_xp_effects(ctx, me.member->account_id));
    if (!claim.claimed) {
        return Err(error_not_available);
    }
    store_progress(ctx, me.member->account_id, before, claim.update.progress);
    return Ok();
}

// =============================================================================
// Views
// =============================================================================

// The tables, views and reducers this module has (protocol_version in
// game/net_types.h): a game subscribes to this before anything else, and
// plays only when it matches its own.
SPACETIMEDB_VIEW(std::optional<server_protocol_info>, server_protocol, Public, AnonymousViewContext ctx) {
    (void)ctx;
    return server_protocol_info{protocol_version};
}

// What only the caller may see.

SPACETIMEDB_VIEW(std::optional<player_row>, my_account, Public, ViewContext ctx) {
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    return login ? ctx.db[player_account_id].find(login->account_id) : std::nullopt;
}

SPACETIMEDB_VIEW(std::optional<link_code_view>, my_link_code, Public, ViewContext ctx) {
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    if (!login) {
        return std::nullopt;
    }
    const std::optional<link_code_row> code = ctx.db[link_code_account_id].find(login->account_id);
    return code ? std::optional<link_code_view>(link_code_view{code->code, code->expires_at}) : std::nullopt;
}

SPACETIMEDB_VIEW(std::optional<link_status>, my_link_status, Public, ViewContext ctx) {
    const std::optional<link_attempt_row> attempt = ctx.db[link_attempt_identity].find(ctx.sender());
    return attempt ? std::optional<link_status>(link_status{attempt->last_result, attempt->last_at}) : std::nullopt;
}
