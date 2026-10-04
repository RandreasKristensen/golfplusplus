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
#include "module_cache.h"
#include "physics/vector_math.h"
#include "play_rules.h"
#include "server_errors.h"

#include <spacetimedb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
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
    std::uint32_t steam_app_id;
    bool allow_non_steam;
    std::string link_secret;  // keys link codes (make_link_code); empty: linking is off
};
SPACETIMEDB_STRUCT(server_config_row, id, owner, auth_issuer, auth_audience, allow_anonymous, steam_app_id, allow_non_steam,
                   link_secret)
SPACETIMEDB_TABLE(server_config_row, server_config, Private)
FIELD_PrimaryKey(server_config, id)

// An account. display_name is empty until claimed; name_key is its
// case-folded form, unique among named accounts.
struct player_row {
    std::uint64_t account_id;
    std::string display_name;
    std::string name_key;
    bool online;
    std::int32_t holes_completed;  // the clock for collectible cooldowns, as in save_data
    Timestamp created_at;
    Timestamp last_login;
};
SPACETIMEDB_STRUCT(player_row, account_id, display_name, name_key, online, holes_completed, created_at, last_login)
SPACETIMEDB_TABLE(player_row, player, Public)
FIELD_PrimaryKeyAutoInc(player, account_id)
FIELD_Index(player, name_key)

// One login identity of an account.
struct account_login_row {
    Identity identity;
    std::uint64_t account_id;
    std::string login_method;  // login_method_name
    std::string steam_id;      // Steam logins only, for friends later; never a key
    Timestamp linked_at;
};
SPACETIMEDB_STRUCT(account_login_row, identity, account_id, login_method, steam_id, linked_at)
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

// A player in a room. zone is hub_zone (-1) or the hole being played.
// round_strokes is this round's score per hole (0: not played yet), which
// the group's scorecard shows.
struct room_member_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint64_t group_id;  // 0: none
    std::int32_t zone;
    Timestamp hole_started_at;
    std::vector<std::int32_t> round_strokes;
};
SPACETIMEDB_STRUCT(room_member_row, account_id, room_id, group_id, zone, hole_started_at, round_strokes)
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
    std::int32_t zone;
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
SPACETIMEDB_STRUCT(avatar_motion_row, account_id, room_id, zone, mode, x, y, z, yaw, speed, turn_rate, client_time,
                   server_time)
SPACETIMEDB_TABLE(avatar_motion_row, avatar_motion, Public)
FIELD_PrimaryKey(avatar_motion, account_id)
FIELD_Index(avatar_motion, room_id)

// A player's ball on the hole they play. Deleted when holed or abandoned.
// rests_at is when the last shot's ball stops (server time): the next shot
// or a retee waits for it, as offline waits for the shot to play.
struct ball_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::int32_t zone;
    float x;
    float y;
    float z;
    std::int32_t stroke_count;
    float last_wind_time;
    Timestamp rests_at;
};
SPACETIMEDB_STRUCT(ball_row, account_id, room_id, zone, x, y, z, stroke_count, last_wind_time, rests_at)
SPACETIMEDB_TABLE(ball_row, ball, Public)
FIELD_PrimaryKey(ball, account_id)
FIELD_Index(ball, room_id)

// The server's view of a player's movement: the last accepted position and
// mode, the motion slack left (check_motion), the movement XP meters not yet
// earned, and when they last smoked and drank. Like offline play state, the
// cigarette, the emotes and the pending meters end when the player moves
// between the hub and a hole.
struct motion_budget_row {
    std::uint64_t account_id;
    float x;
    float y;
    float z;
    std::uint8_t mode;  // motion_mode
    Timestamp server_time;
    float slack_left;
    Timestamp smoke_at;  // the epoch: never
    Timestamp drink_at;
    float walk_meters_pending;
    float cart_meters_pending;
    float drift_meters_pending;
};
SPACETIMEDB_STRUCT(motion_budget_row, account_id, x, y, z, mode, server_time, slack_left, smoke_at, drink_at,
                   walk_meters_pending, cart_meters_pending, drift_meters_pending)
SPACETIMEDB_TABLE(motion_budget_row, motion_budget, Private)
FIELD_PrimaryKey(motion_budget, account_id)

// A shot someone in the room hit, with everything needed to simulate it
// again (shot_input) and where the server's simulation put the ball.
struct shot_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;
    std::int32_t zone;
    std::int32_t stroke;
    float start_x;
    float start_y;
    float start_z;
    float aim_angle;
    std::string club_id;
    float power;
    bool cigarette_active;
    float wind_time;
    float rest_x;
    float rest_y;
    float rest_z;
    bool holed;
};
SPACETIMEDB_STRUCT(shot_event_row, room_id, account_id, zone, stroke, start_x, start_y, start_z, aim_angle, club_id, power,
                   cigarette_active, wind_time, rest_x, rest_y, rest_z, holed)
SPACETIMEDB_TABLE(shot_event_row, shot_event, Public, true)
FIELD_Index(shot_event, room_id)

struct emote_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;
    std::string emote_id;  // emote_name in game/net_types.h
};
SPACETIMEDB_STRUCT(emote_event_row, room_id, account_id, emote_id)
SPACETIMEDB_TABLE(emote_event_row, emote_event, Public, true)
FIELD_Index(emote_event, room_id)

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

// =============================================================================
// Helpers
// =============================================================================

namespace {
constexpr std::uint32_t config_id = 0;
constexpr std::int64_t micros_per_second = 1000000;
constexpr std::int64_t micros_per_hour = 3600 * micros_per_second;

std::int64_t minutes_in_micros(const float minutes) {
    return static_cast<std::int64_t>(static_cast<double>(minutes) * 60.0 * static_cast<double>(micros_per_second));
}

ConnectionId caller_connection(const ReducerContext& ctx) {
    return ctx.connection_id.value_or(ConnectionId());
}

login_config config_rules(const server_config_row& config) {
    return login_config{config.auth_issuer, config.auth_audience, config.allow_anonymous, config.steam_app_id,
                        config.allow_non_steam};
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
    ctx.db[avatar_motion_account_id].delete_by_key(account_id);
    ctx.db[ball_account_id].delete_by_key(account_id);
    ctx.db[motion_budget_account_id].delete_by_key(account_id);
    ctx.db[room_member_account_id].delete_by_key(account_id);
    if (std::optional<room_row> joined = ctx.db[room_room_id].find(member->room_id)) {
        joined->player_count -= 1;
        if (joined->player_count <= 0) {
            ctx.db[room_room_id].delete_by_key(joined->room_id);
        } else {
            ctx.db[room_room_id].update(*joined);
        }
    }
}

// Puts the player at `position` (course coordinates) in member.zone, as the
// server's last accepted position and the avatar others see, and ends the
// play state that offline ends there too (reset_play_state).
void place_player(ReducerContext& ctx, const room_member_row& member, const glm::vec3& position) {
    if (std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(member.account_id)) {
        *budget = motion_budget_row{member.account_id, position.x, position.y, position.z,
                                    static_cast<std::uint8_t>(motion_mode::idle), ctx.timestamp,
                                    cached_content()->tuning.server.motion_distance_slack, Timestamp(), Timestamp(),
                                    0.0f, 0.0f, 0.0f};
        ctx.db[motion_budget_account_id].update(*budget);
    }
    if (std::optional<avatar_motion_row> avatar = ctx.db[avatar_motion_account_id].find(member.account_id)) {
        avatar->zone = member.zone;
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

// The hole `member` plays. A course republished with fewer holes may no
// longer have it: then the player is sent back to the hub (and the reducer
// returns Ok, so that is kept) instead of reading past the course's holes.
struct played_hole {
    std::optional<std::size_t> hole;
    bool sent_back = false;
};

played_hole current_hole(ReducerContext& ctx, room_member_row& member, const server_course* course) {
    if (member.zone == hub_zone || course == nullptr) {
        return {};
    }
    const std::size_t hole = static_cast<std::size_t>(member.zone);
    if (hole < course->holes.size()) {
        return {hole, false};
    }
    ctx.db[ball_account_id].delete_by_key(member.account_id);
    member.zone = hub_zone;
    ctx.db[room_member_account_id].update(member);
    place_player(ctx, member, anchor_on_terrain(course->area, course->world.hole_starts.front().position));
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
    ctx.db[server_config].insert(server_config_row{config_id, ctx.sender(), "", "", false, 0, true, ""});
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
    const login_check check = check_login(claims, config_rules(*config), ctx.sender() == config->owner);
    if (!check.method) {
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
            ctx.db[player].insert(player_row{0, "", "", false, 0, ctx.timestamp, ctx.timestamp});
        login = account_login_row{ctx.sender(), created.account_id, login_method_name(*check.method), check.steam_id,
                                  ctx.timestamp};
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
                    std::uint32_t steam_app_id,
                    bool allow_non_steam,
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
    config->steam_app_id = steam_app_id;
    config->allow_non_steam = allow_non_steam;
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
        delete_link_code(ctx, from);
        ctx.db[link_budget_account_id].delete_by_key(from);
        ctx.db[player_account_id].delete_by_key(from);
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
// hole 1's start in the hub, as offline.
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
    ctx.db[room_member].insert(room_member_row{account, joined->room_id, 0, hub_zone, ctx.timestamp, unplayed});
    ctx.db[motion_budget].insert(motion_budget_row{account, spawn.x, spawn.y, spawn.z,
                                                   static_cast<std::uint8_t>(motion_mode::idle), ctx.timestamp,
                                                   cached_content()->tuning.server.motion_distance_slack, Timestamp(),
                                                   Timestamp(), 0.0f, 0.0f, 0.0f});
    ctx.db[avatar_motion].insert(avatar_motion_row{account, joined->room_id, hub_zone,
                                                   static_cast<std::uint8_t>(motion_mode::idle), spawn.x, spawn.y, spawn.z,
                                                   0.0f, 0.0f, 0.0f, 0.0, ctx.timestamp});
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
                    std::int32_t zone,
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
    if (zone != me.member->zone) {
        return Err(error_wrong_zone);
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
    previous.zone = zone;
    previous.mode = static_cast<motion_mode>(budget->mode);
    previous.position = from;
    previous.yaw = last ? last->yaw : 0.0f;
    previous.speed = last ? last->speed : 0.0f;
    previous.turn_rate = last ? last->turn_rate : 0.0f;
    motion_check move =
        check_motion(previous, to, seconds_between(budget->server_time, ctx.timestamp), mode, budget->slack_left, content.tuning);
    // Addressing the ball steps the player around it at once (address
    // position), as offline does from beside a ball at rest: allowed from and
    // to within reach of their own ball once its shot has played. It earns
    // nothing.
    if (!move.allowed && mode == static_cast<std::uint8_t>(motion_mode::aim)) {
        const std::optional<ball_row> own_ball = ctx.db[ball_account_id].find(me.member->account_id);
        const float reach = content.tuning.player.ball_interact_radius;
        if (own_ball && shot_has_played(*own_ball, ctx.timestamp, content.tuning.server)) {
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

    // Movement XP as offline, for the stretch since the last update in the
    // mode it was covered in (an update is sent when the mode changes, so
    // the new mode starts here). Progress is only read when a rate has
    // gathered enough meters for XP.
    const float meters = move.earned_distance * content.tuning.scale.meters_per_world_unit;
    const bool on_road = zone == hub_zone && on_cart_road(course->world.cart_roads, from, content.tuning.cart);
    const movement_earnings earns = movement_earns(budget->mode, on_road);
    const reward_rules& rewards = content.rewards;
    struct earning {
        bool earns;
        const movement_xp_rate* rate;
        float* pending;
    };
    const earning earnings[] = {{earns.walking, &rewards.walking, &budget->walk_meters_pending},
                                {earns.cart, &rewards.cart_on_road, &budget->cart_meters_pending},
                                {earns.drift, &rewards.drift_on_road, &budget->drift_meters_pending}};
    const bool xp_due = std::any_of(std::begin(earnings), std::end(earnings), [meters](const earning& e) {
        return e.earns && *e.pending + meters >= e.rate->meters_per_xp;
    });
    if (xp_due) {
        const save_data before = load_progress(ctx, me.member->account_id);
        save_data after = before;
        for (const earning& e : earnings) {
            if (e.earns) {
                const movement_xp_update update = award_movement_xp(after, *e.rate, *e.pending, meters);
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
    const float shown_speed = clamp_motion_speed(mode, speed, content.tuning);
    const float shown_turn_rate = clamp_turn_rate(turn_rate, content.tuning);
    ctx.db[avatar_motion_account_id].update(avatar_motion_row{me.member->account_id, me.member->room_id, zone, mode, x, y, z,
                                                              yaw, shown_speed, shown_turn_rate, client_time, ctx.timestamp});
    return Ok();
}

// Starts a hole from its start in the hub, with the ball on the tee.
SPACETIMEDB_REDUCER(enter_hole, ReducerContext ctx, std::int32_t hole_index) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->zone != hub_zone) {
        return Err(error_not_in_hub);
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
    const course_world_hole_start& start = course->world.hole_starts[index];
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget || !within_interact_reach(position_of(*budget), start.position, start.interaction_radius,
                                          cached_content()->tuning.server)) {
        return Err(error_too_far);
    }

    me.member->zone = hole_index;
    me.member->hole_started_at = ctx.timestamp;
    ctx.db[room_member_account_id].update(*me.member);
    const server_content& content = *cached_content();
    const float radius = content.tuning.scale.ball_physics_radius_meters;
    const glm::vec3 tee = resting_ball_position(course->area, course->holes[index].tee_position, radius);
    ctx.db[ball].insert(
        ball_row{me.member->account_id, me.member->room_id, hole_index, tee.x, tee.y, tee.z, 0, 0.0f, ctx.timestamp});
    place_player(ctx, *me.member,
                 tee_stance_position(course->area, tee, course->shot_holes[index].pin, content.tuning.player.ball_stand_off_distance));
    return Ok();
}

// Gives up the hole: no score, back at the hole's start, where it was
// entered (its return point would make this a free trip across the course).
SPACETIMEDB_REDUCER(return_to_hub, ReducerContext ctx) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const server_course* course = member_course(ctx, *me.member);
    const played_hole on = current_hole(ctx, *me.member, course);
    if (!on.hole) {
        return on.sent_back ? Ok() : Err(error_not_on_hole);
    }
    const course_world_hole_start& start = course->world.hole_starts[*on.hole];
    ctx.db[ball_account_id].delete_by_key(me.member->account_id);
    me.member->zone = hub_zone;
    ctx.db[room_member_account_id].update(*me.member);
    place_player(ctx, *me.member, anchor_on_terrain(course->area, start.position));
    return Ok();
}

// Puts the ball back on the tee without a penalty stroke, as retee_ball
// offline.
SPACETIMEDB_REDUCER(retee, ReducerContext ctx) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<ball_row> ball_now = ctx.db[ball_account_id].find(me.member->account_id);
    const server_course* course = member_course(ctx, *me.member);
    const played_hole on = current_hole(ctx, *me.member, course);
    if (!on.hole) {
        return on.sent_back ? Ok() : Err(error_not_on_hole);
    }
    if (!ball_now) {
        return Err(error_not_on_hole);
    }
    const server_content& content = *cached_content();
    if (!shot_has_played(*ball_now, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    const float radius = content.tuning.scale.ball_physics_radius_meters;
    const glm::vec3 tee = resting_ball_position(course->area, course->holes[*on.hole].tee_position, radius);
    ball_now->x = tee.x;
    ball_now->y = tee.y;
    ball_now->z = tee.z;
    ball_now->last_wind_time = 0.0f;
    ctx.db[ball_account_id].update(*ball_now);
    // Offline a retee restarts the hole's clock (hole_time), so the wind does too.
    room_member_row member = *me.member;
    member.hole_started_at = ctx.timestamp;
    ctx.db[room_member_account_id].update(member);
    place_player(ctx, member,
                 tee_stance_position(course->area, tee, course->shot_holes[*on.hole].pin, content.tuning.player.ball_stand_off_distance));
    return Ok();
}

// Simulates the shot (simulate_shot, as every client does from the event)
// and decides where the ball rests, the stroke, and a holed ball's score and
// completions, as complete_current_hole does offline.
SPACETIMEDB_REDUCER(take_shot,
                    ReducerContext ctx,
                    std::int32_t stroke,
                    float aim_angle,
                    std::string club_id,
                    float power,
                    bool cigarette_active,
                    float wind_time) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    std::optional<ball_row> ball_now = ctx.db[ball_account_id].find(me.member->account_id);
    const server_course* course = member_course(ctx, *me.member);
    const played_hole on = current_hole(ctx, *me.member, course);
    if (!on.hole) {
        return on.sent_back ? Ok() : Err(error_not_on_hole);
    }
    if (!ball_now) {
        return Err(error_not_on_hole);
    }
    const server_content& content = *cached_content();
    const std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    const bool smoked = budget && budget->smoke_at.micros_since_epoch() > 0;
    // The client lights its cigarette by its own clock: allow it the
    // timing slack at the end too.
    const float cigarette_seconds = content.rewards.cigarette.duration_seconds + content.tuning.server.timing_slack_seconds;
    const bool cigarette_lit = smoked && seconds_between(budget->smoke_at, ctx.timestamp) <= cigarette_seconds;
    const glm::vec3 player_position = budget ? position_of(*budget) : glm::vec3(ball_now->x, ball_now->y, ball_now->z);
    if (!shot_has_played(*ball_now, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    const shot_check check = check_shot(
        shot_request{stroke, aim_angle, club_id, power, cigarette_active, wind_time},
        server_ball{glm::vec3(ball_now->x, ball_now->y, ball_now->z), ball_now->stroke_count, ball_now->last_wind_time},
        player_position, seconds_between(me.member->hole_started_at, ctx.timestamp), cigarette_lit, content);
    if (!check.input) {
        return Err(check.error);
    }
    const std::size_t hole = *on.hole;
    const shot_input& input = *check.input;
    const shot_result result = simulate_shot(input, shot_course{course->area, course->trees, course->shot_holes[hole]},
                                             content.tuning, content.clubs, content.rewards);

    const std::uint64_t account = me.member->account_id;
    const save_data before = load_progress(ctx, account);
    save_data after = award_xp(before, content.rewards.shot).progress;
    ctx.db[shot_event].insert(shot_event_row{me.member->room_id, account, me.member->zone, stroke, input.ball_start.x,
                                             input.ball_start.y, input.ball_start.z, input.aim_angle, input.club_id,
                                             input.power, input.cigarette_active, input.wind_time, result.rest_position.x,
                                             result.rest_position.y, result.rest_position.z, result.holed});

    if (!result.holed) {
        ball_now->x = result.rest_position.x;
        ball_now->y = result.rest_position.y;
        ball_now->z = result.rest_position.z;
        ball_now->stroke_count = stroke;
        ball_now->last_wind_time = input.wind_time;
        ball_now->rests_at = ctx.timestamp + TimeDuration::from_micros(static_cast<std::int64_t>(
                                                 static_cast<double>(result.duration) * static_cast<double>(micros_per_second)));
        ctx.db[ball_account_id].update(*ball_now);
        store_progress(ctx, account, before, after);
        return Ok();
    }

    ctx.db[hole_score].insert(hole_score_row{0, account, course->course.id, me.member->zone, stroke,
                                             course->holes[hole].par, ctx.timestamp});
    after = apply_hole_completed(after);
    me.member->round_strokes.resize(course->holes.size(), 0);
    me.member->round_strokes[hole] = stroke;
    if (round_finished(current_round(*me.member))) {
        // Rooms are courses with a world, never practice holes, so the round
        // always completes the course. A new round starts.
        after = apply_course_completed(after, course->course.id);
        std::fill(me.member->round_strokes.begin(), me.member->round_strokes.end(), 0);
    }
    store_progress(ctx, account, before, after);
    ctx.db[ball_account_id].delete_by_key(account);
    me.member->zone = hub_zone;
    ctx.db[room_member_account_id].update(*me.member);
    place_player(ctx, *me.member, anchor_on_terrain(course->area, course->world.hole_starts[hole].return_position));
    return Ok();
}

// Smoking lights a cigarette (shots honour it for its duration) and earns
// the smoke XP, as offline; everyone in the room sees the emote. As offline,
// an emote cannot start again while it still plays (player.emote_seconds).
SPACETIMEDB_REDUCER(emote, ReducerContext ctx, std::string emote_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    const std::optional<::emote_id> chosen = emote_from_name(emote_id);
    if (!chosen) {
        return Err(error_unknown_emote);
    }
    std::optional<motion_budget_row> budget = ctx.db[motion_budget_account_id].find(me.member->account_id);
    if (!budget) {
        return Err(error_not_in_room);
    }
    const server_content& content = *cached_content();
    // Not while the caller's shot plays, as offline.
    const std::optional<ball_row> own_ball = ctx.db[ball_account_id].find(me.member->account_id);
    if (own_ball && !shot_has_played(*own_ball, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    Timestamp& last = *chosen == ::emote_id::smoke ? budget->smoke_at : budget->drink_at;
    // The client times the emote by its own frames, so it may ask a little
    // before the server's clock says the last one ended.
    const float playing_seconds = content.tuning.player.emote_seconds - content.tuning.server.timing_slack_seconds;
    const bool playing = last.micros_since_epoch() > 0 && seconds_between(last, ctx.timestamp) < playing_seconds;
    if (playing) {
        return Err(error_too_soon);
    }
    last = ctx.timestamp;
    if (*chosen == ::emote_id::smoke) {
        const save_data before = load_progress(ctx, me.member->account_id);
        store_progress(ctx, me.member->account_id, before, award_xp(before, content.rewards.smoke).progress);
    }
    ctx.db[motion_budget_account_id].update(*budget);
    ctx.db[emote_event].insert(emote_event_row{me.member->room_id, me.member->account_id, emote_id});
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
    if (me.member->zone != hub_zone || course == nullptr) {
        return Err(error_not_in_hub);
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
    const claim_update claim = claim_collectible(before, *found);
    if (!claim.claimed) {
        return Err(error_not_available);
    }
    store_progress(ctx, me.member->account_id, before, claim.update.progress);
    return Ok();
}

// =============================================================================
// Views: what only the caller may see
// =============================================================================

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
