// The golf++ SpacetimeDB module: its tables, views and reducers.
// SpacetimeDB's macros register everything from one translation unit, so the
// whole schema and every reducer live here. Decisions are made by the plain
// rule files next to it (account_rules.h, server_content.h), which the
// native tests check; this file only reads and writes rows.
//
// Accounts are not logins: every login identity maps to one account
// (account_login). Progress is a golfer's: an account's in one scope
// (golfer). Every progress table is Private and read through a my_* view.

#include "account_rules.h"
#include "cart_rules.h"
#include "interest_rules.h"
#include "game/net_types.h"
#include "game/play_area.h"
#include "game/progress_rules.h"
#include "game/progression.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "game/weather.h"
#include "module_cache.h"
#include "movement_rules.h"
#include "physics/vector_math.h"
#include "play_rules.h"
#include "game/hub_scene.h"
#include "game/hub_ball.h"
#include "tee_rules.h"
#include "game/dap_rules.h"
#include "progress_rollup.h"
#include "game/server_errors.h"

#include <spacetimedb.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
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
    // Keys link codes (make_link_code) and shot seeds (server_shot_seed);
    // empty: linking is off and seeds can be worked out.
    std::string server_secret;
};
SPACETIMEDB_STRUCT(server_config_row, id, owner, auth_issuer, auth_audience, allow_anonymous, server_secret)
SPACETIMEDB_TABLE(server_config_row, server_config, Private)
FIELD_PrimaryKey(server_config, id)

// An account. display_name is empty until claimed; name_key is its
// case-folded form, unique among named accounts. As a golfer (set_golfer):
// their hand (handedness), natural shape (game/golfer.h natural_shape,
// natural_shape_unset until picked) and favourite colour (game/gear_paint.h,
// 0 for none).
struct player_row {
    std::uint64_t account_id;
    std::string display_name;
    std::string name_key;
    bool online;
    Timestamp created_at;
    Timestamp last_login;
    std::uint8_t hand;
    std::uint8_t natural_shape;
    std::uint8_t favourite_colour;
};
SPACETIMEDB_STRUCT(player_row, account_id, display_name, name_key, online, created_at, last_login, hand, natural_shape,
                   favourite_colour)
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

// A golfer's progress: an account's in one scope. Scope 0
// (permanent_scope) is the account's own; a season will be a scope of its
// own. Its tables below, keyed by golfer_id,
// mirror save_data so progress_rules runs on the server unchanged.
// holes_completed is the clock for collectible cooldowns, as in save_data.
struct golfer_progress_row {
    std::uint64_t golfer_id;
    std::uint64_t account_id;
    std::uint32_t scope_id;
    std::int32_t holes_completed;
    Timestamp created_at;
};
SPACETIMEDB_STRUCT(golfer_progress_row, golfer_id, account_id, scope_id, holes_completed, created_at)
SPACETIMEDB_TABLE(golfer_progress_row, golfer_progress, Private)
FIELD_PrimaryKeyAutoInc(golfer_progress, golfer_id)
FIELD_Index(golfer_progress, account_id)

// A skill's XP: the sum of its xp_award and xp_rollup rows. No level: that
// follows from the skill's curve (game/skill_catalog.h).
struct golfer_skill_row {
    std::uint64_t id;
    std::uint64_t golfer_id;
    std::string skill_id;
    std::int64_t xp;
};
SPACETIMEDB_STRUCT(golfer_skill_row, id, golfer_id, skill_id, xp)
SPACETIMEDB_TABLE(golfer_skill_row, golfer_skill, Private)
FIELD_PrimaryKeyAutoInc(golfer_skill, id)
FIELD_Index(golfer_skill, golfer_id)

// Every XP award, never changed: what earned it (`source`) and the event it
// came from (`dedupe_key`, unique, so one event pays once). A correction is
// another award, negative if need be. Kept server.xp_award_keep_days, then
// summed into xp_rollup (roll_up_xp): by then no request can come again,
// and a one-off collectible stays claimed by its collected row.
struct xp_award_row {
    std::uint64_t id;
    std::string dedupe_key;
    std::uint64_t golfer_id;
    std::string skill_id;
    std::int64_t amount;
    std::string source;  // award_source_*
    Timestamp at;
};
SPACETIMEDB_STRUCT(xp_award_row, id, dedupe_key, golfer_id, skill_id, amount, source, at)
SPACETIMEDB_TABLE(xp_award_row, xp_award, Private)
FIELD_PrimaryKeyAutoInc(xp_award, id)
FIELD_Unique(xp_award, dedupe_key)
FIELD_Index(xp_award, golfer_id)
FIELD_Index(xp_award, at)

// A golfer's awards of a skill in a calendar month (UTC, yyyymm), summed
// once they are older than server.xp_award_keep_days (progress_rollup.h).
struct xp_rollup_row {
    std::uint64_t id;
    std::string key;  // rollup_key
    std::uint64_t golfer_id;
    std::string skill_id;
    std::int32_t month;
    std::int64_t amount;
};
SPACETIMEDB_STRUCT(xp_rollup_row, id, key, golfer_id, skill_id, month, amount)
SPACETIMEDB_TABLE(xp_rollup_row, xp_rollup, Private)
FIELD_PrimaryKeyAutoInc(xp_rollup, id)
FIELD_Unique(xp_rollup, key)
FIELD_Index(xp_rollup, golfer_id)

// Runs roll_up_xp every server.xp_rollup_interval_hours: one row.
struct xp_rollup_run_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
};
SPACETIMEDB_STRUCT(xp_rollup_run_row, scheduled_id, scheduled_at)
SPACETIMEDB_TABLE(xp_rollup_run_row, xp_rollup_run, Private)
FIELD_PrimaryKeyAutoInc(xp_rollup_run, scheduled_id)
SPACETIMEDB_SCHEDULE(xp_rollup_run, 1, roll_up_xp)

struct hole_score_row {
    std::uint64_t id;
    std::uint64_t golfer_id;
    std::string course_id;
    std::int32_t hole_index;
    std::int32_t strokes;
    std::int32_t par;
    Timestamp at;
};
SPACETIMEDB_STRUCT(hole_score_row, id, golfer_id, course_id, hole_index, strokes, par, at)
SPACETIMEDB_TABLE(hole_score_row, hole_score, Private)
FIELD_PrimaryKeyAutoInc(hole_score, id)
FIELD_Index(hole_score, golfer_id)

struct completed_course_row {
    std::uint64_t id;
    std::uint64_t golfer_id;
    std::string course_id;
    Timestamp at;
};
SPACETIMEDB_STRUCT(completed_course_row, id, golfer_id, course_id, at)
SPACETIMEDB_TABLE(completed_course_row, completed_course, Private)
FIELD_PrimaryKeyAutoInc(completed_course, id)
FIELD_Index(completed_course, golfer_id)

// A claimed collectible. Repeatable ones keep their claim count and the
// golfer's holes_completed at the last claim (-1: never).
struct collected_row {
    std::uint64_t id;
    std::uint64_t golfer_id;
    std::string collectible_id;
    bool repeatable;
    std::int32_t claim_count;
    std::int32_t claimed_at_holes_completed;
};
SPACETIMEDB_STRUCT(collected_row, id, golfer_id, collectible_id, repeatable, claim_count, claimed_at_holes_completed)
SPACETIMEDB_TABLE(collected_row, collected, Private)
FIELD_PrimaryKeyAutoInc(collected, id)
FIELD_Index(collected, golfer_id)

struct world_flag_row {
    std::uint64_t id;
    std::uint64_t golfer_id;
    std::string flag;
};
SPACETIMEDB_STRUCT(world_flag_row, id, golfer_id, flag)
SPACETIMEDB_TABLE(world_flag_row, world_flag, Private)
FIELD_PrimaryKeyAutoInc(world_flag, id)
FIELD_Index(world_flag, golfer_id)

// A persistent course world. World 1 is the only world allocated today.
struct room_row {
    std::uint64_t room_id;
    std::string course_id;
    std::int32_t player_count;
    Timestamp created_at;
    std::uint32_t world_id;
    std::uint8_t kind;
};
SPACETIMEDB_STRUCT(room_row, room_id, course_id, player_count, created_at, world_id, kind)
SPACETIMEDB_TABLE(room_row, room, Public)
FIELD_PrimaryKeyAutoInc(room, room_id)
FIELD_Index(room, course_id)

// A player in a room. round_strokes is this round's score per hole (0: not
// played yet), which the group's scorecard shows. Completed scores stay
// until the member tees up the next round.
struct room_member_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint64_t group_id;  // 0: none
    std::vector<std::int32_t> round_strokes;
    bool round_started;
};
SPACETIMEDB_STRUCT(room_member_row, account_id, room_id, group_id, round_strokes, round_started)
SPACETIMEDB_TABLE(room_member_row, room_member, Public)
FIELD_PrimaryKey(room_member, account_id)
FIELD_Index(room_member, room_id)
FIELD_Index(room_member, group_id)

// Present players sharing a booked slot's scorecard. Offline reservations
// count in tee_slot.booked_count, never in this presence count.
struct golf_group_row {
    std::uint64_t group_id;
    std::uint64_t room_id;
    std::int32_t member_count;
};
SPACETIMEDB_STRUCT(golf_group_row, group_id, room_id, member_count)
SPACETIMEDB_TABLE(golf_group_row, golf_group, Public)
FIELD_PrimaryKey(golf_group, group_id)
FIELD_Index(golf_group, room_id)

#include "tee_schema.h"

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
    // At their ball: the setup they stand to (game/sent_setup.h), inside
    // the club in hand's limits.
    std::uint16_t setup_aim;
    std::int16_t setup_face_open;
    std::int8_t setup_ball_position;
    std::uint16_t setup_backswing;
    bool crouched;  // on foot: crouched reading the green
    Timestamp server_time;
    std::int32_t area_x;
    std::int32_t area_z;
    std::string area_key;
};
SPACETIMEDB_STRUCT(avatar_motion_row, account_id, room_id, mode, x, y, z, yaw, speed, turn_rate, client_time, setup_aim,
                   setup_face_open, setup_ball_position, setup_backswing, crouched, server_time, area_x, area_z, area_key)
SPACETIMEDB_TABLE(avatar_motion_row, avatar_motion, Private)
FIELD_PrimaryKey(avatar_motion, account_id)
FIELD_Index(avatar_motion, room_id)
FIELD_Index(avatar_motion, area_key)

#include "dap_schema.h"

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
    std::uint8_t kind;
    std::int32_t tee_mat;
};
SPACETIMEDB_STRUCT(ball_row, account_id, room_id, hole, x, y, z, stroke_count, last_shot_at, rests_at, in_cup, kind, tee_mat)
SPACETIMEDB_TABLE(ball_row, ball, Public)
FIELD_PrimaryKey(ball, account_id)
FIELD_Index(ball, room_id)

// A player's golf cart (game/golf_cart.h), there while they are in the
// room: parked beside hole 1 when they join (free_parking_slot), then where
// their last motion at its wheel left it (x, y, z the ground under the
// middle of its seat). `passenger` rides in its other seat (board_cart).
// `condition` is a cart_condition (physics/cart_dynamics.h): the server's
// steps of its driver tip or sink it (update_motion), anyone beside it
// rights it (right_cart), and a sunk one is towed back (tow_cart).
struct golf_cart_row {
    std::uint64_t owner;
    std::uint64_t room_id;
    float x;
    float y;
    float z;
    float yaw;
    std::uint64_t passenger;  // 0: nobody
    std::uint8_t condition;
    std::uint64_t driver;
    double motion_id;
    std::int32_t area_x;
    std::int32_t area_z;
    std::string area_key;
};
SPACETIMEDB_STRUCT(golf_cart_row, owner, room_id, x, y, z, yaw, passenger, condition, driver, motion_id, area_x, area_z, area_key)
SPACETIMEDB_TABLE(golf_cart_row, golf_cart, Private)
FIELD_PrimaryKey(golf_cart, owner)
FIELD_Index(golf_cart, room_id)
FIELD_Index(golf_cart, passenger)
FIELD_Index(golf_cart, area_key)

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

// The server's view of a player's movement (movement_rules.h mover_state):
// where its steps have them and how they move up and down, the last step
// played and the last motion accepted, the time saved up for more steps,
// the claimed-move slack (check_motion), their cart's motion while they
// drive it, and their movement XP: meters not yet earned, and earned this
// hour (from hour_started).
struct motion_state_row {
    std::uint64_t account_id;
    float x;
    float y;
    float z;
    std::uint8_t mode;  // motion_mode
    Timestamp server_time;
    float slack_left;
    float walk_meters_pending;
    float cart_meters_pending;
    std::uint64_t step;
    float credit_seconds;
    double client_time;
    float vertical_speed;
    bool grounded;
    float cart_velocity_x;
    float cart_velocity_z;
    float cart_yaw_rate;
    float cart_lift_seconds;
    float cart_vertical_speed;
    bool cart_grounded;
    Timestamp hour_started;
    float walk_meters_this_hour;
    float cart_meters_this_hour;
};
SPACETIMEDB_STRUCT(motion_state_row, account_id, x, y, z, mode, server_time, slack_left, walk_meters_pending,
                   cart_meters_pending, step, credit_seconds, client_time, vertical_speed, grounded, cart_velocity_x,
                   cart_velocity_z, cart_yaw_rate, cart_lift_seconds, cart_vertical_speed, cart_grounded, hour_started,
                   walk_meters_this_hour, cart_meters_this_hour)
SPACETIMEDB_TABLE(motion_state_row, motion_state, Private)
FIELD_PrimaryKey(motion_state, account_id)

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

// A golfer's level in a skill, as a shot used it.
struct skill_level_entry {
    std::string skill_id;
    std::int32_t level;
};
SPACETIMEDB_STRUCT(skill_level_entry, skill_id, level)

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
    float strike_timing;
    std::uint8_t hand;
    // The golfer as the server played them: their natural shape, the spread
    // their items gave them and their levels before the shot (game/golfer.h).
    std::uint8_t natural_shape;
    float spread_scale;
    std::vector<skill_level_entry> levels;
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
                   ball_position, backswing, strike_timing, hand, natural_shape, spread_scale, levels, club_id, shot_at, seed,
                   flags_out, snapshot,
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
// hand: on their cart behind its driver's seat (or on their back without
// one) with the first club in hand when they join; moved by place_bag, a
// club taken from it by take_club. Strapped on a cart that leaves the room
// or is towed back, it is set down where the cart was.
struct player_bag_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    std::uint8_t place;  // bag_where
    std::uint8_t seat;   // cart_seat, on_cart
    std::uint64_t cart_owner;  // on_cart: whose cart, 0 their own
    float x;             // on_ground
    float y;
    float z;
    float yaw;
    std::string club_in_hand;
};
SPACETIMEDB_STRUCT(player_bag_row, account_id, room_id, place, seat, cart_owner, x, y, z, yaw, club_in_hand)
SPACETIMEDB_TABLE(player_bag_row, player_bag, Public)
FIELD_PrimaryKey(player_bag, account_id)
FIELD_Index(player_bag, room_id)

// The cart a rider sits in as its driver drives it, for the rider alone
// (they subscribe to their own row): the driver's motion at its wheel,
// sent far more often than avatar_motion while someone rides with them
// (update_ride, game/ride_sync.h). There while they ride.
struct ride_motion_row {
    std::uint64_t rider;
    std::uint64_t owner;
    std::uint8_t mode;  // motion_mode, cart
    float x;
    float y;
    float z;
    float yaw;
    float speed;
    float turn_rate;
    double client_time;
    Timestamp server_time;
};
SPACETIMEDB_STRUCT(ride_motion_row, rider, owner, mode, x, y, z, yaw, speed, turn_rate, client_time, server_time)
SPACETIMEDB_TABLE(ride_motion_row, ride_motion, Public)
FIELD_PrimaryKey(ride_motion, rider)
FIELD_Index(ride_motion, owner)

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

struct server_content_info {
    std::uint64_t hash;
};
SPACETIMEDB_STRUCT(server_content_info, hash)

// =============================================================================
// Helpers
// =============================================================================

namespace {
constexpr std::uint32_t config_id = 0;
constexpr std::uint32_t permanent_scope = 0;
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

// --- Golfers: an account's progress, by scope.

// Every table of a golfer's progress, as its rows of the golfer and the
// table by primary key: has_progress and delete_account go through this one
// list, so a table added here is in both.
template <typename Visit>
void each_golfer_table(ReducerContext& ctx, const std::uint64_t golfer_id, Visit&& visit) {
    visit(ctx.db[golfer_skill_golfer_id].filter(golfer_id), ctx.db[golfer_skill_id]);
    visit(ctx.db[xp_award_golfer_id].filter(golfer_id), ctx.db[xp_award_id]);
    visit(ctx.db[xp_rollup_golfer_id].filter(golfer_id), ctx.db[xp_rollup_id]);
    visit(ctx.db[hole_score_golfer_id].filter(golfer_id), ctx.db[hole_score_id]);
    visit(ctx.db[completed_course_golfer_id].filter(golfer_id), ctx.db[completed_course_id]);
    visit(ctx.db[collected_golfer_id].filter(golfer_id), ctx.db[collected_id]);
    visit(ctx.db[world_flag_golfer_id].filter(golfer_id), ctx.db[world_flag_id]);
}

// The account's golfer in `scope`, if it has one.
template <typename Context>
std::optional<golfer_progress_row> golfer_in(const Context& ctx, const std::uint64_t account_id, const std::uint32_t scope) {
    for (const golfer_progress_row& golfer : ctx.db[golfer_progress_account_id].filter(account_id)) {
        if (golfer.scope_id == scope) {
            return golfer;
        }
    }
    return std::nullopt;
}

// The account's permanent golfer, made with no progress when it has none.
golfer_progress_row permanent_golfer(ReducerContext& ctx, const std::uint64_t account_id) {
    if (const std::optional<golfer_progress_row> found = golfer_in(ctx, account_id, permanent_scope)) {
        return *found;
    }
    return ctx.db[golfer_progress].insert(golfer_progress_row{0, account_id, permanent_scope, 0, ctx.timestamp});
}

bool has_progress(ReducerContext& ctx, const player_row& player) {
    if (!player.display_name.empty()) {
        return true;
    }
    bool any = false;
    for (const golfer_progress_row& each : ctx.db[golfer_progress_account_id].filter(player.account_id)) {
        any = any || each.holes_completed > 0;
        each_golfer_table(ctx, each.golfer_id, [&any](auto&& rows, auto&&) { any = any || rows.begin() != rows.end(); });
    }
    return any;
}


glm::vec3 position_of(const motion_state_row& mover) {
    return glm::vec3(mover.x, mover.y, mover.z);
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
    ctx.db[ride_motion_rider].delete_by_key(account);
    return left;
}

float seconds_between(const Timestamp& earlier, const Timestamp& later) {
    return static_cast<float>(static_cast<double>(later.micros_since_epoch() - earlier.micros_since_epoch()) /
                              static_cast<double>(micros_per_second));
}

mover_state mover_of(const motion_state_row& row) {
    mover_state mover;
    mover.mode = static_cast<motion_mode>(row.mode);
    mover.position = position_of(row);
    mover.vertical = vertical_body{row.y, row.vertical_speed, row.grounded};
    mover.step = row.step;
    mover.credit_seconds = row.credit_seconds;
    mover.slack_left = row.slack_left;
    mover.client_time = row.client_time;
    mover.cart_velocity = glm::vec2(row.cart_velocity_x, row.cart_velocity_z);
    mover.cart_yaw_rate = row.cart_yaw_rate;
    mover.cart_lift_seconds = row.cart_lift_seconds;
    mover.cart_vertical = vertical_body{0.0f, row.cart_vertical_speed, row.cart_grounded};
    return mover;
}

void write_mover(motion_state_row& row, const mover_state& mover) {
    row.mode = static_cast<std::uint8_t>(mover.mode);
    row.x = mover.position.x;
    row.y = mover.position.y;
    row.z = mover.position.z;
    row.vertical_speed = mover.vertical.speed;
    row.grounded = mover.vertical.grounded;
    row.step = mover.step;
    row.credit_seconds = mover.credit_seconds;
    row.slack_left = mover.slack_left;
    row.client_time = mover.client_time;
    row.cart_velocity_x = mover.cart_velocity.x;
    row.cart_velocity_z = mover.cart_velocity.y;
    row.cart_yaw_rate = mover.cart_yaw_rate;
    row.cart_lift_seconds = mover.cart_lift_seconds;
    row.cart_vertical_speed = mover.cart_vertical.speed;
    row.cart_grounded = mover.cart_vertical.grounded;
}

// A player just in the room at `spawn`: a full slack and time saved up.
motion_state_row joined_motion_state(const std::uint64_t account, const glm::vec3& spawn, const Timestamp& now) {
    const server_tuning& tuning = cached_content()->tuning.server;
    motion_state_row row{account, spawn.x, spawn.y, spawn.z, static_cast<std::uint8_t>(motion_mode::idle), now,
                         tuning.motion_distance_slack, 0.0f, 0.0f, 0, tuning.max_motion_gap_seconds, 0.0, 0.0f, true,
                         0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true, now, 0.0f, 0.0f};
    return row;
}

// Puts the mover standing at `position` now, the time since saved up for
// its steps.
void place_mover(motion_state_row& row, const glm::vec3& position, const Timestamp& now) {
    mover_state mover = mover_of(row);
    mover.credit_seconds = std::min(cached_content()->tuning.server.max_motion_gap_seconds,
                                    mover.credit_seconds + std::max(0.0f, seconds_between(row.server_time, now)));
    mover = placed_mover(mover, position);
    mover.mode = motion_mode::idle;
    mover.slack_left = cached_content()->tuning.server.motion_distance_slack;
    write_mover(row, mover);
    row.server_time = now;
}

// --- Progress: the account's rows as a save_data, so progress_rules runs on
// the server exactly as offline, and back.

save_data load_progress(ReducerContext& ctx, const golfer_progress_row& owner) {
    save_data progress;
    progress.holes_completed = owner.holes_completed;
    for (const golfer_skill_row& skill : ctx.db[golfer_skill_golfer_id].filter(owner.golfer_id)) {
        progress.skills[skill.skill_id].xp = skill.xp;
    }
    for (const completed_course_row& course : ctx.db[completed_course_golfer_id].filter(owner.golfer_id)) {
        progress.completed_course_ids.push_back(course.course_id);
    }
    for (const collected_row& claim : ctx.db[collected_golfer_id].filter(owner.golfer_id)) {
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
    for (const world_flag_row& flag : ctx.db[world_flag_golfer_id].filter(owner.golfer_id)) {
        progress.world_flags.push_back(flag.flag);
    }
    return progress;
}

bool contains(const std::vector<std::string>& values, const std::string& value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

// What earned an award (xp_award_row::source).
constexpr const char* award_source_shot = "shot";
constexpr const char* award_source_movement = "movement";
constexpr const char* award_source_item = "item";
constexpr const char* award_source_collectible = "collectible";

// The event awards come from: what it was and a key naming it, unique to it.
struct award_event {
    const char* source;
    std::string key;
};

// Pays one award into the golfer's skill and records it, unless its event
// already paid that skill.
void pay_award(ReducerContext& ctx, const std::uint64_t golfer_id, const awarded_xp& award, const award_event& event) {
    const std::string dedupe_key = event.key + ":" + award.skill_id;
    if (award.xp == 0 || ctx.db[xp_award_dedupe_key].find(dedupe_key)) {
        return;
    }
    ctx.db[xp_award].insert(xp_award_row{0, dedupe_key, golfer_id, award.skill_id, award.xp, event.source, ctx.timestamp});
    for (golfer_skill_row skill : ctx.db[golfer_skill_golfer_id].filter(golfer_id)) {
        if (skill.skill_id == award.skill_id) {
            skill.xp += award.xp;
            ctx.db[golfer_skill_id].update(skill);
            return;
        }
    }
    ctx.db[golfer_skill].insert(golfer_skill_row{0, golfer_id, award.skill_id, award.xp});
}

// Writes what progress_rules changed from `before` (it only ever adds) to
// the golfer's rows. XP goes in only as `update`'s awards, from `event`.
void store_progress(ReducerContext& ctx, golfer_progress_row owner, const save_data& before, const progress_update& update,
                    const award_event& event) {
    const save_data& after = update.progress;
    for (const awarded_xp& award : update.awarded) {
        pay_award(ctx, owner.golfer_id, award, event);
    }
    if (after.holes_completed != before.holes_completed) {
        owner.holes_completed = after.holes_completed;
        ctx.db[golfer_progress_golfer_id].update(owner);
    }
    for (const std::string& course_id : after.completed_course_ids) {
        if (!contains(before.completed_course_ids, course_id)) {
            ctx.db[completed_course].insert(completed_course_row{0, owner.golfer_id, course_id, ctx.timestamp});
        }
    }
    for (const std::string& collectible_id : after.collected_ids) {
        if (!contains(before.collected_ids, collectible_id)) {
            ctx.db[collected].insert(collected_row{0, owner.golfer_id, collectible_id, false, 1, -1});
        }
    }
    for (const auto& [collectible_id, state] : after.repeatable_collectibles) {
        const auto earlier = before.repeatable_collectibles.find(collectible_id);
        if (earlier != before.repeatable_collectibles.end() && earlier->second.claim_count == state.claim_count) {
            continue;
        }
        const collected_row row{0, owner.golfer_id, collectible_id, true, state.claim_count,
                                state.claimed_at_holes_completed.value_or(-1)};
        std::optional<collected_row> existing;
        for (const collected_row& claim : ctx.db[collected_golfer_id].filter(owner.golfer_id)) {
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
            ctx.db[world_flag].insert(world_flag_row{0, owner.golfer_id, flag});
        }
    }
}

void erase_reservation(ReducerContext& ctx, const tee_reservation_row& booking);

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
    ctx.db[golf_group_group_id].update(*group);
}

// Takes the account out of its room: group, avatar, ball (abandoning the
// hole, so no score) and membership; the course world remains when empty.
// Every bag strapped on `owner`'s cart set down on the ground beside where
// it stands (`cart`), at its driver's door.
void set_down_bags_from(ReducerContext& ctx, const std::uint64_t room_id, const std::uint64_t owner, const cart_pose& cart,
                        const server_course& course) {
    const cart_tuning& tuning = cached_content()->tuning.cart;
    std::vector<player_bag_row> strapped;
    for (const player_bag_row& bag : ctx.db[player_bag_room_id].filter(room_id)) {
        const std::uint64_t on = bag.cart_owner != 0 ? bag.cart_owner : bag.account_id;
        if (bag.place == static_cast<std::uint8_t>(bag_where::on_cart) && on == owner) {
            strapped.push_back(bag);
        }
    }
    for (player_bag_row bag : strapped) {
        const glm::vec3 at = anchor_on_terrain(course.area, step_out_position(cart, static_cast<cart_seat>(bag.seat), tuning));
        bag.place = static_cast<std::uint8_t>(bag_where::on_ground);
        bag.cart_owner = 0;
        bag.x = at.x;
        bag.y = at.y;
        bag.z = at.z;
        bag.yaw = cart.yaw;
        ctx.db[player_bag_account_id].update(bag);
    }
}

#include "dap_support.h"

void leave_room_state(ReducerContext& ctx, const std::uint64_t account_id) {
    erase_daps(ctx, account_id);
    std::optional<room_member_row> member = ctx.db[room_member_account_id].find(account_id);
    if (!member) {
        return;
    }
    if (auto booking = ctx.db[tee_reservation_account_id].find(account_id)) {
        if (auto slot = ctx.db[tee_slot_slot_id].find(booking->slot_id); slot && slot->room_id == member->room_id) {
            booking->round_strokes = member->round_strokes;
            ++booking->revision;
            ctx.db[tee_reservation_reservation_id].update(*booking);
        }
    }
    leave_group_state(ctx, *member);
    leave_passenger_seat(ctx, member->room_id, account_id);
    if (const std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(account_id)) {
        if (const server_course* course = member_course(ctx, *member)) {
            set_down_bags_from(ctx, member->room_id, account_id, pose_of(*cart), *course);
        }
        if (cart->passenger != 0) {
            leave_passenger_seat(ctx, member->room_id, cart->passenger);
        }
    }
    ctx.db[golf_cart_owner].delete_by_key(account_id);
    ctx.db[player_bag_account_id].delete_by_key(account_id);
    ctx.db[avatar_motion_account_id].delete_by_key(account_id);
    ctx.db[ball_account_id].delete_by_key(account_id);
    ctx.db[motion_state_account_id].delete_by_key(account_id);
    ctx.db[room_member_account_id].delete_by_key(account_id);
    if (std::optional<room_row> joined = ctx.db[room_room_id].find(member->room_id)) {
        joined->player_count -= 1;
        ctx.db[room_room_id].update(*joined);
    }
}

// Only accepted positions choose interest cells. This is presentation;
// collision and shot snapshots still read every row in the room.
template <typename Row>
void index_motion(Row& row) {
    const game_tuning& tuning = cached_content()->tuning;
    const float width = tuning.server.motion_cell_meters / tuning.scale.meters_per_world_unit;
    const motion_cell area = motion_area(glm::vec3(row.x, row.y, row.z), width);
    row.area_x = area.x;
    row.area_z = area.z;
    row.area_key = motion_area_key(row.room_id, area);
}

// Puts the player at `position` (course coordinates) at once, as the
// server's last accepted position and the avatar others see, standing: as
// teeing up stands them behind the ball offline. Nothing else of theirs
// changes (an item in use, its effects, the meters not yet earned).
void place_player(ReducerContext& ctx, const room_member_row& member, const glm::vec3& position) {
    if (std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(member.account_id)) {
        place_mover(*mover, position, ctx.timestamp);
        ctx.db[motion_state_account_id].update(*mover);
    }
    if (std::optional<avatar_motion_row> avatar = ctx.db[avatar_motion_account_id].find(member.account_id)) {
        avatar->mode = static_cast<std::uint8_t>(motion_mode::idle);
        avatar->x = position.x;
        avatar->y = position.y;
        avatar->z = position.z;
        avatar->speed = 0.0f;
        avatar->turn_rate = 0.0f;
        avatar->crouched = false;
        avatar->server_time = ctx.timestamp;
        index_motion(*avatar);
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
    if ((ball->kind == static_cast<std::uint8_t>(ball_kind::practice) && course->kind == place_kind::hub) ||
        (ball->hole >= 0 && static_cast<std::size_t>(ball->hole) < course->holes.size())) {
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
// logins, link rows and every golfer's progress.
void delete_account(ReducerContext& ctx, const std::uint64_t account_id) {
    if (auto booking = ctx.db[tee_reservation_account_id].find(account_id)) {
        erase_reservation(ctx, *booking);
    }
    leave_room_state(ctx, account_id);
    ctx.db[session_account_id].delete_by_key(account_id);
    ctx.db[tee_book_status_account_id].delete_by_key(account_id);
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
    std::vector<std::uint64_t> golfers;
    for (const golfer_progress_row& each : ctx.db[golfer_progress_account_id].filter(account_id)) {
        golfers.push_back(each.golfer_id);
    }
    for (const std::uint64_t golfer_id : golfers) {
        each_golfer_table(ctx, golfer_id, [](auto&& rows, auto&& by_id) { delete_rows(rows, by_id); });
        ctx.db[golfer_progress_golfer_id].delete_by_key(golfer_id);
    }
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
    bag.cart_owner = row.cart_owner;
    bag.position = glm::vec3(row.x, row.y, row.z);
    bag.yaw = row.yaw;
    return bag;
}

// The cart `account_id`'s bag at `place` names (on_cart): its own, or the
// owner's it is strapped to, in their room.
std::optional<golf_cart_row> bag_cart(ReducerContext& ctx, const bag_place& place, const std::uint64_t account_id,
                                      const std::uint64_t room_id) {
    if (place.where != bag_where::on_cart) {
        return std::nullopt;
    }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(place.cart_owner != 0 ? place.cart_owner : account_id);
    return cart && cart->room_id == room_id ? cart : std::nullopt;
}

std::optional<cart_pose> bag_cart_pose(ReducerContext& ctx, const bag_place& place, const std::uint64_t account_id,
                                       const std::uint64_t room_id) {
    const std::optional<golf_cart_row> cart = bag_cart(ctx, place, account_id, room_id);
    return cart ? std::optional<cart_pose>(pose_of(*cart)) : std::nullopt;
}

// Whether a bag other than `account_id`'s is strapped behind `seat` of `owner`'s cart.
bool bag_spot_taken(ReducerContext& ctx, const std::uint64_t room_id, const std::uint64_t owner, const cart_seat seat,
                    const std::uint64_t account_id) {
    for (const player_bag_row& other : ctx.db[player_bag_room_id].filter(room_id)) {
        const std::uint64_t on = other.cart_owner != 0 ? other.cart_owner : other.account_id;
        if (other.account_id != account_id && other.place == static_cast<std::uint8_t>(bag_where::on_cart) && on == owner &&
            other.seat == static_cast<std::uint8_t>(seat)) {
            return true;
        }
    }
    return false;
}

// The account as a golfer: its hand and natural shape from its row, the
// spread its items give it now and its levels in `progress`.
golfer_profile golfer_of(ReducerContext& ctx, const player_row& player, const save_data& progress) {
    golfer_profile golfer;
    golfer.levels = skill_levels(cached_content()->skills, progress.skills);
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
// The rollup's run, unless the database has it already.
void schedule_xp_rollup(ReducerContext& ctx) {
    if (ctx.db[xp_rollup_run].count() != 0) {
        return;
    }
    const double hours = static_cast<double>(cached_content()->tuning.server.xp_rollup_interval_hours);
    const TimeDuration every = TimeDuration::from_micros(static_cast<std::int64_t>(hours * 3600.0 * 1000000.0));
    ctx.db[xp_rollup_run].insert(xp_rollup_run_row{0, ScheduleAt::interval(every)});
}

SPACETIMEDB_INIT(init, ReducerContext ctx) {
    if (cached_content() == nullptr) {
        return Err("embedded content does not load: " + content_error());
    }
    ctx.db[server_config].insert(server_config_row{config_id, ctx.sender(), "", "", false, ""});
    schedule_xp_rollup(ctx);
    return Ok();
}

// Sums every award older than server.xp_award_keep_days into its golfer's
// rollup for that skill and month, and deletes it. Skill totals don't change.
SPACETIMEDB_REDUCER(roll_up_xp, ReducerContext ctx, xp_rollup_run_row run) {
    static_cast<void>(run);
    if (ctx.sender() != ctx.database_identity()) {
        return Err(error_internal_only);
    }
    const Timestamp cutoff = Timestamp::from_micros_since_epoch(
        rollup_cutoff_micros(ctx.timestamp.micros_since_epoch(), cached_content()->tuning.server.xp_award_keep_days));
    std::vector<rolled_award> expired;
    std::vector<std::uint64_t> ids;
    for (const xp_award_row& award : ctx.db[xp_award_at].filter(range_to(cutoff))) {
        expired.push_back(rolled_award{award.golfer_id, award.skill_id, award.amount, award.at.micros_since_epoch()});
        ids.push_back(award.id);
    }
    for (const std::uint64_t id : ids) {
        ctx.db[xp_award_id].delete_by_key(id);
    }
    for (const rollup_sum& sum : rolled_up(expired)) {
        const std::string key = rollup_key(sum.golfer_id, sum.skill_id, sum.month);
        if (std::optional<xp_rollup_row> row = ctx.db[xp_rollup_key].find(key)) {
            row->amount += sum.amount;
            ctx.db[xp_rollup_id].update(*row);
        } else {
            ctx.db[xp_rollup].insert(xp_rollup_row{0, key, sum.golfer_id, sum.skill_id, sum.month, sum.amount});
        }
    }
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
            ctx.db[player].insert(player_row{0, "", "", false, ctx.timestamp, ctx.timestamp,
                                             static_cast<std::uint8_t>(handedness::right), natural_shape_unset, 0});
        login = account_login_row{ctx.sender(), created.account_id, login_method_name(*method), ctx.timestamp};
        ctx.db[account_login].insert(*login);
    }
    start_session(ctx, login->account_id);
    permanent_golfer(ctx, login->account_id);
    // A database made before the rollup existed gets its run here.
    schedule_xp_rollup(ctx);
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
                    std::string server_secret) {
    std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    if (!config || ctx.sender() != config->owner) {
        return Err(error_owner_only);
    }
    if (!server_secret.empty() && server_secret.size() < min_server_secret_length) {
        return Err(error_invalid_config);
    }
    config->auth_issuer = auth_issuer;
    config->auth_audience = auth_audience;
    config->allow_anonymous = allow_anonymous;
    config->server_secret = server_secret;
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
    if (!config || config->server_secret.size() < min_server_secret_length) {
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
    std::string code = make_link_code(config->server_secret, nonce + "0");
    for (int attempt = 1; ctx.db[link_code_code].find(code); ++attempt) {
        code = make_link_code(config->server_secret, nonce + std::to_string(attempt));
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
    if (!config || config->server_secret.size() < min_server_secret_length) {
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

#include "tee_reducers.h"

// =============================================================================
// Playing
// =============================================================================

// The client moved: its fixed steps since its last motion are played from
// the server's own state (movement_rules.h play_move), and they decide where
// it is, its cart, and the movement XP, as offline. A refused motion leaves
// every row as it was.
namespace {
room_cart cart_value(const golf_cart_row& row) {
    return {glm::vec3(row.x, row.y, row.z), row.yaw, row.passenger,
            static_cast<cart_condition>(row.condition), row.driver, row.motion_id};
}
void write_cart(golf_cart_row& row, const room_cart& cart) {
    row.x = cart.position.x; row.y = cart.position.y; row.z = cart.position.z;
    row.yaw = cart.yaw; row.passenger = cart.passenger;
    row.condition = static_cast<std::uint8_t>(cart.condition);
    row.driver = cart.driver; row.motion_id = cart.motion_id;
}

// Each green's flag in its cup but those out in the room.
std::vector<bool> room_flags_in(ReducerContext& ctx, const std::uint64_t room_id, const std::size_t holes) {
    std::vector<bool> flags_in(holes, true);
    for (const int out : room_flags_out(ctx, room_id)) {
        if (out >= 0 && static_cast<std::size_t>(out) < holes) {
            flags_in[static_cast<std::size_t>(out)] = false;
        }
    }
    return flags_in;
}

// What `member`'s steps meet in its room now, and what its items do.
move_world room_move_world(ReducerContext& ctx, const room_member_row& member, const server_course& course) {
    const server_content& content = *cached_content();
    const std::uint64_t account = member.account_id;
    const std::int64_t now = now_unix_ms(ctx);
    const std::vector<active_effect> effects = account_effects(ctx, account);
    const cart_spec& spec = account_cart_spec(ctx, account);
    move_world world{course.area, course.world.cart_roads, content.tuning};
    world.walk_speed = effective_value(content.tuning.player.walk_speed, effect_target_walk_speed, effects, now);
    world.spec = effective_cart_spec(spec, effects, now);
    world.grip_scale = cart_grip_scale(effects, now);
    world.wetness = weather_at(content.tuning.weather, content.tuning.wind, content.tuning.sky_clock, course.course, now).wetness;
    world.limits = movement_limits_for(content.tuning, spec, effects, now);
    std::vector<cart_pose> carts;
    std::vector<cart_pose> others;
    for (const golf_cart_row& cart : ctx.db[golf_cart_room_id].filter(member.room_id)) {
        carts.push_back(pose_of(cart));
        if (cart.owner != account) {
            others.push_back(pose_of(cart));
        }
    }
    std::vector<glm::vec3> people;
    for (const avatar_motion_row& avatar : ctx.db[avatar_motion_room_id].filter(member.room_id)) {
        const motion_mode mode = static_cast<motion_mode>(avatar.mode);
        if (avatar.account_id != account && (mode == motion_mode::walk || mode == motion_mode::idle)) {
            people.push_back(glm::vec3(avatar.x, avatar.y, avatar.z));
        }
    }
    const std::vector<bool> flags_in = room_flags_in(ctx, member.room_id, course.area.cups.size());
    world.walker_obstacles = walker_obstacles(carts, course.area.cups, flags_in, content.tuning);
    world.cart_obstacles = cart_obstacles_of(others, people, course.area.cups, flags_in, content.tuning);
    const std::optional<ball_row> own_ball = ctx.db[ball_account_id].find(account);
    if (own_ball && !own_ball->in_cup && shot_has_played(*own_ball, ctx.timestamp, content.tuning.server)) {
        world.own_ball = glm::vec3(own_ball->x, own_ball->y, own_ball->z);
    }
    return world;
}

// A sunk cart is towed back after its spec's tow_seconds.
void schedule_tow(ReducerContext& ctx, const std::uint64_t owner) {
    const cart_spec& spec = account_cart_spec(ctx, owner);
    const std::int64_t tow_micros =
        static_cast<std::int64_t>(static_cast<double>(spec.tow_seconds) * static_cast<double>(micros_per_second));
    const Timestamp towed_at = ctx.timestamp + TimeDuration::from_micros(tow_micros);
    ctx.db[cart_tow].insert(cart_tow_row{0, ScheduleAt::time(towed_at), owner});
}

// Takes `rider` out of `cart`'s passenger seat, standing beside it.
void put_out_rider(ReducerContext& ctx, const std::uint64_t rider, const cart_pose& cart, const server_course& course) {
    ctx.db[ride_motion_rider].delete_by_key(rider);
    if (const std::optional<room_member_row> member = ctx.db[room_member_account_id].find(rider)) {
        const glm::vec3 out = step_out_position(cart, cart_seat::passenger, cached_content()->tuning.cart);
        place_player(ctx, *member, glm::vec3(out.x, terrain_height(course.area, out), out.z));
    }
}

// Movement XP for the meters walked and driven on roads, as offline, for
// at most server.walk_xp_meters_per_hour and cart_xp_meters_per_hour an hour.
void pay_movement(ReducerContext& ctx, motion_state_row& mover, const std::uint64_t account, const float walked,
                  const float driven) {
    const server_content& content = *cached_content();
    constexpr float seconds_per_hour = 3600.0f;
    if (seconds_between(mover.hour_started, ctx.timestamp) >= seconds_per_hour) {
        mover.hour_started = ctx.timestamp;
        mover.walk_meters_this_hour = 0.0f;
        mover.cart_meters_this_hour = 0.0f;
    }
    const reward_rules& rewards = content.rewards;
    struct earning {
        float meters;
        const movement_xp_rate* rate;
        float* pending;
    };
    const earning earnings[] = {
        {earning_meters(walked, mover.walk_meters_this_hour, content.tuning.server.walk_xp_meters_per_hour), &rewards.walking,
         &mover.walk_meters_pending},
        {earning_meters(driven, mover.cart_meters_this_hour, content.tuning.server.cart_xp_meters_per_hour),
         &rewards.cart_on_road, &mover.cart_meters_pending}};
    const bool xp_due = std::any_of(std::begin(earnings), std::end(earnings), [](const earning& e) {
        return e.meters > 0.0f && *e.pending + e.meters >= e.rate->meters_per_xp;
    });
    if (!xp_due) {
        for (const earning& e : earnings) {
            *e.pending += e.meters;
        }
        return;
    }
    const golfer_progress_row owner = permanent_golfer(ctx, account);
    const save_data before = load_progress(ctx, owner);
    const xp_effects boosts = account_xp_effects(ctx, account);
    progress_update earned{before, {}};
    for (const earning& e : earnings) {
        const movement_xp_update update = award_movement_xp(earned.progress, *e.rate, *e.pending, e.meters, boosts);
        earned.progress = update.update.progress;
        earned.awarded.insert(earned.awarded.end(), update.update.awarded.begin(), update.update.awarded.end());
        *e.pending = update.remainder_meters;
    }
    store_progress(ctx, owner, before, earned,
                   award_event{award_source_movement, "movement:" + std::to_string(owner.golfer_id) + ":" +
                                                          std::to_string(ctx.timestamp.micros_since_epoch())});
}
}

// One run of fixed steps with the same input (game/movement_step.h move_run).
struct move_run_row {
    std::uint8_t kind;  // move_kind
    std::uint16_t steps;
    std::int8_t forward;
    std::int8_t side;
    bool run;
    bool jump;
    std::uint16_t yaw;
    std::int16_t steer;
    bool paced;
    bool crouched;
};
SPACETIMEDB_STRUCT(move_run_row, kind, steps, forward, side, run, jump, yaw, steer, paced, crouched)

SPACETIMEDB_REDUCER(update_motion,
                    ReducerContext ctx, std::uint64_t expected_room,
                    std::uint8_t mode,
                    float x,
                    float y,
                    float z,
                    float yaw,
                    float speed,
                    float turn_rate,
                    double client_time,
                    std::uint64_t first_step,
                    float start_x,
                    float start_y,
                    float start_z,
                    std::vector<move_run_row> runs,
                    std::uint16_t setup_aim,
                    std::int16_t setup_face_open,
                    std::int8_t setup_ball_position,
                    std::uint16_t setup_backswing,
                    bool crouched) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (mode >= motion_mode_count) {
        return Err(error_invalid_motion);
    }
    const std::uint64_t account = me.member->account_id;
    std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    const server_course* course = member_course(ctx, *me.member);
    if (!mover || course == nullptr) {
        return Err(error_not_in_room);
    }
    if (mode == static_cast<std::uint8_t>(motion_mode::ride)) {
        bool riding = false;
        for (const golf_cart_row& cart : ctx.db[golf_cart_room_id].filter(me.member->room_id)) {
            riding = riding || cart.passenger == account;
        }
        if (!riding) {
            return Err(error_not_in_cart);
        }
    }
    move_request request;
    request.motion = net_motion{static_cast<motion_mode>(mode), glm::vec3(x, y, z), yaw, speed, turn_rate, false, client_time};
    request.motion.crouched = crouched;
    request.first_step = first_step;
    request.start = glm::vec3(start_x, start_y, start_z);
    request.runs.reserve(runs.size());
    for (const move_run_row& run : runs) {
        request.runs.push_back(move_run{move_input{static_cast<move_kind>(run.kind), run.forward, run.side, run.run, run.jump,
                                                   run.yaw, run.steer, run.paced, run.crouched},
                                        run.steps});
    }
    if (dap_busy(ctx, account)) {
        request.motion.mode = motion_mode::idle;
        request.motion.position = position_of(*mover);
        request.motion.speed = 0.0f; request.motion.turn_rate = 0.0f;
        request.motion.crouched = false;
        request.start = position_of(*mover);
        for (auto& run : request.runs) {
            const auto yaw = run.input.yaw;
            run.input = move_input{}; run.input.yaw = yaw;
        }
    }
    std::optional<golf_cart_row> own_cart = ctx.db[golf_cart_owner].find(account);
    const std::optional<room_cart> cart = own_cart ? std::optional<room_cart>(cart_value(*own_cart)) : std::nullopt;
    const move_outcome moved = play_move(mover_of(*mover), cart, account, request,
                                         seconds_between(mover->server_time, ctx.timestamp),
                                         room_move_world(ctx, *me.member, *course));
    if (moved.repeated) {
        return Ok();
    }
    if (!moved.error.empty()) {
        return Err(moved.error);
    }
    if (own_cart && moved.cart) {
        const std::uint64_t rider = own_cart->passenger;
        const bool had_wheel = own_cart->driver == account;
        write_cart(*own_cart, *moved.cart);
        if (rider != 0 && own_cart->passenger == 0) {
            put_out_rider(ctx, rider, pose_of(*own_cart), *course);
        } else if (had_wheel && own_cart->driver != account) {
            ctx.db[ride_motion_rider].delete_by_key(rider);
        }
        if (moved.overturned && own_cart->condition == static_cast<std::uint8_t>(cart_condition::sunk)) {
            schedule_tow(ctx, account);
        }
        index_motion(*own_cart);
        ctx.db[golf_cart_owner].update(*own_cart);
    }
    write_mover(*mover, moved.mover);
    mover->server_time = ctx.timestamp;
    if (course->kind == place_kind::hub || me.member->round_started) {
        pay_movement(ctx, *mover, account, moved.walked_meters, moved.road_meters);
    }
    ctx.db[motion_state_account_id].update(*mover);
    const net_motion& shown = moved.shown;
    const std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(account);
    const club_definition* club = bag ? find_club(cached_content()->clubs, bag->club_in_hand) : nullptr;
    const sent_setup setup = at_ball(shown.mode) && club != nullptr
        ? clamped_sent_setup(sent_setup{setup_aim, setup_face_open, setup_ball_position, setup_backswing}, *club,
                             cached_content()->tuning.setup)
        : sent_setup{};
    avatar_motion_row avatar{account, me.member->room_id, static_cast<std::uint8_t>(shown.mode), shown.position.x,
                             shown.position.y, shown.position.z, shown.yaw, shown.speed, shown.turn_rate, client_time,
                             setup.aim, setup.face_open, setup.ball_position, setup.backswing,
                             shown.crouched && (shown.mode == motion_mode::walk || shown.mode == motion_mode::idle),
                             ctx.timestamp, 0, 0, {}};
    index_motion(avatar);
    ctx.db[avatar_motion_account_id].update(avatar);
    return Ok();
}

// Out of `seat` of the cart at `cart`: the player's steps go on from beside
// its door, where the game stands them.
void stand_out_of(ReducerContext& ctx, const std::uint64_t account, const cart_pose& cart, const cart_seat seat,
                  const room_member_row& member) {
    std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    const server_course* course = member_course(ctx, member);
    if (!mover || course == nullptr) {
        return;
    }
    const glm::vec3 out = step_out_position(cart, seat, cached_content()->tuning.cart);
    place_mover(*mover, glm::vec3(out.x, terrain_height(course->area, out), out.z), ctx.timestamp);
    ctx.db[motion_state_account_id].update(*mover);
}

SPACETIMEDB_REDUCER(take_wheel, ReducerContext ctx, std::uint64_t expected_room) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    const std::uint64_t account = me.member->account_id;
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(account);
    std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    if (!cart) { return Err(error_unknown_cart); }
    if (!mover) { return Err(error_not_in_room); }
    const cart_decision decision = take_wheel_rule(cart_value(*cart), account, position_of(*mover), cached_content()->tuning);
    if (!decision.cart) { return Err(decision.error); }
    leave_passenger_seat(ctx, me.member->room_id, account);
    write_cart(*cart, *decision.cart);
    ctx.db[golf_cart_owner].update(*cart);
    // It starts standing, as the game takes it from its row.
    mover->cart_velocity_x = 0.0f;
    mover->cart_velocity_z = 0.0f;
    mover->cart_yaw_rate = 0.0f;
    mover->cart_lift_seconds = 0.0f;
    mover->cart_vertical_speed = 0.0f;
    mover->cart_grounded = true;
    ctx.db[motion_state_account_id].update(*mover);
    return Ok();
}

SPACETIMEDB_REDUCER(leave_wheel, ReducerContext ctx, std::uint64_t expected_room) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(me.member->account_id);
    if (!cart) { return Err(error_unknown_cart); }
    const cart_decision decision = leave_wheel_rule(cart_value(*cart), me.member->account_id);
    if (!decision.cart) { return Err(decision.error); }
    write_cart(*cart, *decision.cart);
    ctx.db[golf_cart_owner].update(*cart);
    ctx.db[ride_motion_rider].delete_by_key(cart->passenger);
    stand_out_of(ctx, me.member->account_id, pose_of(*cart), cart_seat::driver, *me.member);
    return Ok();
}

// Into the passenger seat of `owner`'s cart in my room, from beside it, when
// nobody sits there.
SPACETIMEDB_REDUCER(board_cart, ReducerContext ctx, std::uint64_t expected_room, std::uint64_t owner) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
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
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!mover) {
        return Err(error_not_in_room);
    }
    if (!within_seat_reach(pose_of(*cart), cart_seat::passenger, position_of(*mover), cached_content()->tuning)) {
        return Err(error_too_far);
    }
    leave_passenger_seat(ctx, me.member->room_id, me.member->account_id);
    cart->passenger = me.member->account_id;
    ctx.db[golf_cart_owner].update(*cart);
    return Ok();
}

// Out of the passenger seat I sit in.
SPACETIMEDB_REDUCER(leave_cart, ReducerContext ctx, std::uint64_t expected_room) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    std::optional<cart_pose> riding;
    for (const golf_cart_row& cart : ctx.db[golf_cart_room_id].filter(me.member->room_id)) {
        if (cart.passenger == me.member->account_id) {
            riding = pose_of(cart);
        }
    }
    if (!leave_passenger_seat(ctx, me.member->room_id, me.member->account_id)) {
        return Err(error_not_in_cart);
    }
    stand_out_of(ctx, me.member->account_id, *riding, cart_seat::passenger, *me.member);
    return Ok();
}

// My seat at my cart's wheel for the one riding with me (ride_motion):
// only while the server has me driving with them aboard, and no further
// from my last accepted motion than my cart could have gone since
// (ride_motion_plausible). Presentation only: it moves nothing.
SPACETIMEDB_REDUCER(update_ride,
                    ReducerContext ctx, std::uint64_t expected_room,
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
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const bool finite = std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(yaw) &&
        std::isfinite(speed) && std::isfinite(turn_rate) && std::isfinite(client_time);
    if (!finite) {
        return Err(error_invalid_motion);
    }
    const std::uint64_t account = me.member->account_id;
    const std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(account);
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    if (!cart || !mover || cart->driver != account || mover->mode != static_cast<std::uint8_t>(motion_mode::cart)) {
        return Err(error_not_in_cart);
    }
    if (cart->passenger == 0) {
        return Ok();
    }
    const server_content& content = *cached_content();
    const movement_limits moving = movement_limits_for(content.tuning, account_cart_spec(ctx, account),
                                                       account_effects(ctx, account), now_unix_ms(ctx));
    const glm::vec3 to(x, y, z);
    if (!ride_motion_plausible(position_of(*mover), seconds_between(mover->server_time, ctx.timestamp), to, moving,
                               content.tuning.server)) {
        return Err(error_too_fast);
    }
    const ride_motion_row row{cart->passenger, account, static_cast<std::uint8_t>(motion_mode::cart), x, y, z, yaw,
                              clamp_motion_speed(static_cast<std::uint8_t>(motion_mode::cart), speed, moving, content.tuning.server),
                              clamp_turn_rate(turn_rate, moving), client_time, ctx.timestamp};
    if (ctx.db[ride_motion_rider].find(cart->passenger)) {
        ctx.db[ride_motion_rider].update(row);
    } else {
        ctx.db[ride_motion].insert(row);
    }
    return Ok();
}

// Stands `owner`'s tipped cart in my room back on its wheels, from beside it.
SPACETIMEDB_REDUCER(right_cart, ReducerContext ctx, std::uint64_t expected_room, std::uint64_t owner) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(owner);
    if (!cart || cart->room_id != me.member->room_id) {
        return Err(error_unknown_cart);
    }
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!mover) {
        return Err(error_not_in_room);
    }
    const cart_decision decision = right_cart_rule(cart_value(*cart), position_of(*mover), cached_content()->tuning);
    if (!decision.cart) { return Err(decision.error); }
    write_cart(*cart, *decision.cart);
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
    set_down_bags_from(ctx, cart->room_id, tow.owner, pose_of(*cart), *course);
    const cart_pose parked = free_parking(ctx, cart->room_id, *course);
    const cart_decision decision = tow_cart_rule(cart_value(*cart), parked);
    write_cart(*cart, *decision.cart);
    index_motion(*cart);
    ctx.db[golf_cart_owner].update(*cart);
    return Ok();
}

// Tees the ball up on `hole_index` from that hole's start, the player behind
// it, as tee_up offline: not while the ball is in its cup (pick_up_ball
// first), nor while it is in play on another hole unless `give_up` gives
// that hole up (no score).
SPACETIMEDB_REDUCER(tee_up, ReducerContext ctx, std::uint64_t expected_room, std::int32_t hole_index, bool give_up) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    const server_course* course = member_course(ctx, *me.member);
    if (course && course->hub) {
        if (hole_index < 0 || static_cast<std::size_t>(hole_index) >= course->hub->tee_mats.size()) { return Err(error_unknown_hole); }
        const auto& mat = course->hub->tee_mats[static_cast<std::size_t>(hole_index)];
        const auto mover = ctx.db[motion_state_account_id].find(me.member->account_id);
        const auto own = ctx.db[ball_account_id].find(me.member->account_id);
        const auto& tuning = cached_content()->tuning;
        if (!mover || in_cart(static_cast<motion_mode>(mover->mode)) ||
            !within_interact_reach(position_of(*mover), mat.position, mat.reach, tuning.server)) { return Err(error_too_far); }
        std::map<std::uint64_t, room_ball> balls;
        for (const auto& other : ctx.db[ball_room_id].filter(expected_room)) {
            balls[other.account_id] = {other.hole, {other.x, other.y, other.z}, other.stroke_count, other.in_cup,
                static_cast<ball_kind>(other.kind), other.tee_mat};
        }
        if (tee_in_use(balls, me.member->account_id, hole_index, true)) { return Err(error_tee_in_use); }
        const auto tee = resting_ball_position(course->area, mat.position, tuning.scale.ball_physics_radius_meters);
        const ball_row prepared{me.member->account_id, expected_room, -1, tee.x, tee.y, tee.z, 0, 0, ctx.timestamp, false,
            static_cast<std::uint8_t>(ball_kind::practice), hole_index};
        if (own) { ctx.db[ball_account_id].update(prepared); } else { ctx.db[ball].insert(prepared); }
        place_player(ctx, *me.member, tee_stance_position(course->area, tee, mat.aim, tuning.player.ball_stand_off_distance));
        return Ok();
    }
    if (course == nullptr || hole_index < 0 || static_cast<std::size_t>(hole_index) >= course->holes.size()) {
        return Err(error_unknown_hole);
    }
    if (course->kind == place_kind::course && me.member->round_started) {
        const auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id);
        if (!booking || booking->completed) { return Err(error_booking_required); }
        if (booking->started_at == 0) { return Err(error_first_tee_required); }
    }
    const std::size_t index = static_cast<std::size_t>(hole_index);
    // A republished course may have another hole count than this round.
    me.member->round_strokes.resize(course->holes.size(), 0);
    // On a practice ground every station drops a fresh ball any time, in
    // place of the last wherever it is (game/course_session.h tee_up).
    const bool practice = !me.member->round_started;
    const bool warmup = practice;
    if (!practice && hole_played(current_round(*me.member), index)) {
        return Err(error_hole_played);
    }
    const std::uint64_t account = me.member->account_id;
    const std::optional<ball_row> own = ctx.db[ball_account_id].find(account);
    if (!practice && own && own->in_cup) {
        return Err(error_ball_in_cup);
    }
    if (!practice && own && own->hole == hole_index) {
        return Err(error_ball_in_play);  // that hole's ball is played where it lies
    }
    if (!practice && own && !give_up) {
        return Err(error_ball_in_play);
    }
    std::map<std::uint64_t, room_ball> room_balls;
    for (const ball_row& other : ctx.db[ball_room_id].filter(me.member->room_id)) {
        room_balls[other.account_id] =
            room_ball{other.hole, glm::vec3(other.x, other.y, other.z), other.stroke_count, other.in_cup, static_cast<ball_kind>(other.kind), other.tee_mat};
    }
    if (!warmup && tee_in_use(room_balls, account, hole_index, course->kind == place_kind::hub)) {
        return Err(error_tee_in_use);
    }
    const course_world_hole_start& start = course->world.hole_starts[index];
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    const glm::vec3 station = warmup ? warmup_position(course->area, index, cached_content()->tuning) : start.position;
    const float reach = warmup ? cached_content()->tuning.server.warmup_reach_meters / cached_content()->tuning.scale.meters_per_world_unit : start.interaction_radius;
    if (!mover || in_cart(static_cast<motion_mode>(mover->mode)) || !within_interact_reach(position_of(*mover), station, reach,
                                          cached_content()->tuning.server)) {
        return Err(error_too_far);
    }

    ctx.db[room_member_account_id].update(*me.member);
    const server_content& content = *cached_content();
    const float radius = content.tuning.scale.ball_physics_radius_meters;
    if (warmup) {
        if (auto bag = ctx.db[player_bag_account_id].find(account)) {
            for (const auto& club : content.clubs) {
                if (club.club_class == "putter") {
                    bag->club_in_hand = club.id;
                    ctx.db[player_bag_account_id].update(*bag); break;
                }
            }
        }
    }
    const glm::vec3 tee = resting_ball_position(course->area, warmup ? station : course->holes[index].tee_position, radius);
    const ball_row teed{account, me.member->room_id, hole_index, tee.x, tee.y, tee.z, 0, 0, ctx.timestamp, false, static_cast<std::uint8_t>(warmup ? ball_kind::warmup : ball_kind::scored), -1};
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
SPACETIMEDB_REDUCER(give_up_hole, ReducerContext ctx, std::uint64_t expected_room) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const ball_on_hole on = ball_in_play_of(ctx, me.member->account_id, member_course(ctx, *me.member));
    if (!on.ball) {
        return on.given_up ? Ok() : Err(error_not_on_hole);
    }
    if (on.ball->kind == static_cast<std::uint8_t>(ball_kind::scored) && me.member->round_started) {
        if (auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id); booking && booking->started_at == 0) {
            return Err(error_first_tee_required);
        }
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
                    ReducerContext ctx, std::uint64_t expected_room,
                    std::int32_t stroke,
                    float aim_angle,
                    float face_open,
                    float ball_position,
                    float backswing,
                    float strike_timing,
                    std::string club_id,
                    std::int64_t shot_at,
                    std::vector<std::int32_t> flags_out) {
    member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    const server_course* course = member_course(ctx, *me.member);
    ball_on_hole on = ball_in_play_of(ctx, me.member->account_id, course);
    if (!on.ball) {
        return on.given_up ? Ok() : Err(error_not_on_hole);
    }
    std::optional<ball_row>& ball_now = on.ball;
    const server_content& content = *cached_content();
    if (ball_now->kind == static_cast<std::uint8_t>(ball_kind::warmup)) {
        const club_definition* club = find_club(content.clubs, club_id);
        if (!club || club->club_class != "putter") { return Err(error_not_available); }
    }
    const std::uint64_t account = me.member->account_id;
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(account);
    const glm::vec3 player_position = mover ? position_of(*mover) : glm::vec3(ball_now->x, ball_now->y, ball_now->z);
    if (!shot_has_played(*ball_now, ctx.timestamp, content.tuning.server)) {
        return Err(error_too_soon);
    }
    const std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(account);
    const golfer_progress_row owner = permanent_golfer(ctx, account);
    const save_data before = load_progress(ctx, owner);
    const server_golfer golfer{golfer_of(ctx, *me.player, before), bag ? bag->club_in_hand : std::string(),
                               bag && bag->place == static_cast<std::uint8_t>(bag_where::on_back),
                               room_flags_out(ctx, me.member->room_id)};
    const glm::vec3 from(ball_now->x, ball_now->y, ball_now->z);
    const shot_check check =
        check_shot(shot_request{stroke, aim_angle, face_open, ball_position, backswing, strike_timing, club_id, shot_at,
                                std::vector<int>(flags_out.begin(), flags_out.end())},
                   server_ball{from, ball_now->stroke_count, ball_now->last_shot_at, lie_at(course->area, from)},
                   player_position, golfer, now_unix_ms(ctx), content);
    if (!check.input) {
        return Err(check.error);
    }
    const std::size_t hole = static_cast<std::size_t>(ball_now->hole);
    const shot_setup& input = *check.input;
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    const std::string secret = config ? config->server_secret : std::string();
    if (ball_now->kind == static_cast<std::uint8_t>(ball_kind::scored)) {
        auto booking = ctx.db[tee_reservation_account_id].find(account);
        if (!booking || booking->completed) { return Err(error_booking_required); }
        if (booking->started_at == 0) {
            const auto slot = ctx.db[tee_slot_slot_id].find(booking->slot_id);
            if (!slot || slot->room_id != me.member->room_id) { return Err(error_stale_booking); }
            // start_booked_round already sent this group off.
            const std::string error = check_first_tee(slot->scheduled_at, ctx.timestamp.micros_since_epoch(),
                                                     ball_now->hole, false, true, content.tuning.server);
            if (!error.empty()) { return Err(error); }
            booking->started_at = ctx.timestamp.micros_since_epoch();
            ++booking->revision;
            ctx.db[tee_reservation_reservation_id].update(*booking);
            ctx.db[tee_start].insert(tee_start_row{booking->reservation_id, account, booking->slot_id,
                slot->course_id, slot->world_id, slot->scheduled_at, booking->started_at,
                booking->started_at - slot->scheduled_at});
        }
    }
    const shot_record record{input, server_shot_seed(secret, ctx.timestamp.micros_since_epoch(), account, stroke),
                             room_snapshot(ctx, me.member->room_id, account)};
    const shot_result result =
        simulate_struck_shot(record, golfer.golfer, server_shot_course(*course, hole, input.shot_at, golfer.flags_out, content),
                             content.tuning, content.clubs);

    // Practice earns nothing: no XP, no score, no completion.
    const bool practice = ball_now->kind != static_cast<std::uint8_t>(ball_kind::scored);
    progress_update update =
        practice ? progress_update{before, {}} : award_xp(before, content.rewards.shot, account_xp_effects(ctx, account));
    const award_event event{award_source_shot, "shot:" + std::to_string(owner.golfer_id) + ":" +
                                                   std::to_string(ctx.timestamp.micros_since_epoch())};
    std::vector<skill_level_entry> levels;
    for (const auto& [skill_id, level] : golfer.golfer.levels) {
        levels.push_back(skill_level_entry{skill_id, level});
    }
    ctx.db[shot_event].insert(shot_event_row{
        me.member->room_id, account, ball_now->hole, stroke, input.ball_start.x, input.ball_start.y, input.ball_start.z,
        input.aim_angle, input.face_open, input.ball_position, input.backswing, input.strike_timing,
        static_cast<std::uint8_t>(input.hand),
        static_cast<std::uint8_t>(natural_shape_index(golfer.golfer.natural)), golfer.golfer.spread_scale, levels, input.club_id,
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
        store_progress(ctx, owner, before, update, event);
        return Ok();
    }

    if (!practice) {
        ctx.db[hole_score].insert(hole_score_row{0, owner.golfer_id, course->course.id, ball_now->hole, strokes,
                                                 course->holes[hole].par, ctx.timestamp});
        update.progress = apply_hole_completed(update.progress);
        me.member->round_strokes.resize(course->holes.size(), 0);
        me.member->round_strokes[hole] = strokes;
        if (round_finished(current_round(*me.member))) {
            // Retain the completed card until the next round's first tee-up.
            update.progress = apply_course_completed(update.progress, course->course.id);
        }
        store_progress(ctx, owner, before, update, event);
        ctx.db[room_member_account_id].update(*me.member);
        if (auto booking = ctx.db[tee_reservation_account_id].find(account)) {
            booking->round_strokes = me.member->round_strokes;
            if (round_finished(current_round(*me.member)) && !booking->completed) {
                release_slot_seat(ctx, *booking);
                booking->completed = true;
            }
            ++booking->revision;
            ctx.db[tee_reservation_reservation_id].update(*booking);
        }
    }
    // The ball stays in the cup until it is picked up, as offline; the player
    // stays where they holed out from.
    const glm::vec3 cup = result.rest_position;
    ball_now->x = cup.x;
    ball_now->y = cup.y;
    ball_now->z = cup.z;
    ball_now->stroke_count = strokes;
    ball_now->last_shot_at = input.shot_at;
    ball_now->in_cup = true;
    ball_now->rests_at = ctx.timestamp + TimeDuration::from_micros(static_cast<std::int64_t>(
        static_cast<double>(result.duration) * static_cast<double>(micros_per_second)));
    ctx.db[ball_account_id].update(*ball_now);
    return Ok();
}

// Takes the flag of hole `hole`'s green out (`out`) or puts it back, for
// everyone in the room: from within player.flag_reach of its cup, as P offline.
SPACETIMEDB_REDUCER(set_flag, ReducerContext ctx, std::uint64_t expected_room, std::int32_t hole, bool out) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const server_course* course = member_course(ctx, *me.member);
    if (course == nullptr || hole < 0 || static_cast<std::size_t>(hole) >= course->area.cups.size()) {
        return Err(error_unknown_hole);
    }
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    const game_tuning& tuning = cached_content()->tuning;
    if (!mover || !within_interact_reach(position_of(*mover), course->area.cups[static_cast<std::size_t>(hole)].centre,
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
// and `cart_owner` whose cart (0 mine) onto a cart standing on its wheels,
// the rest where it is set down.
SPACETIMEDB_REDUCER(place_bag, ReducerContext ctx, std::uint64_t expected_room, std::uint8_t place, std::uint8_t seat, std::uint64_t cart_owner, float x,
                    float y, float z, float yaw) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(me.member->account_id);
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!bag || !mover) {
        return Err(error_not_in_room);
    }
    if (place > static_cast<std::uint8_t>(bag_where::on_ground) || seat > static_cast<std::uint8_t>(cart_seat::passenger)) {
        return Err(error_invalid_motion);
    }
    const std::uint64_t account = me.member->account_id;
    const std::uint64_t room_id = me.member->room_id;
    const bag_place to{static_cast<bag_where>(place), static_cast<cart_seat>(seat), cart_owner == account ? 0 : cart_owner,
                       glm::vec3(x, y, z), yaw};
    const std::optional<golf_cart_row> onto = bag_cart(ctx, to, account, room_id);
    if (onto && onto->condition != static_cast<std::uint8_t>(cart_condition::upright)) {
        return Err(error_cart_overturned);
    }
    const bool taken = onto && bag_spot_taken(ctx, room_id, onto->owner, to.seat, account);
    const std::string error =
        check_bag_move(bag_of(*bag), to, position_of(*mover), bag_cart_pose(ctx, bag_of(*bag), account, room_id),
                       onto ? std::optional<cart_pose>(pose_of(*onto)) : std::nullopt, taken, cached_content()->tuning);
    if (!error.empty()) {
        return Err(error);
    }
    bag->place = place;
    bag->seat = seat;
    bag->cart_owner = to.cart_owner;
    bag->x = x;
    bag->y = y;
    bag->z = z;
    bag->yaw = yaw;
    ctx.db[player_bag_account_id].update(*bag);
    return Ok();
}

// Takes `club_id` from my bag into my hand (check_take_club), putting back
// the one I held.
SPACETIMEDB_REDUCER(take_club, ReducerContext ctx, std::uint64_t expected_room, std::string club_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    std::optional<player_bag_row> bag = ctx.db[player_bag_account_id].find(me.member->account_id);
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!bag || !mover) {
        return Err(error_not_in_room);
    }
    const std::string error =
        check_take_club(club_id, bag_of(*bag), position_of(*mover),
                        bag_cart_pose(ctx, bag_of(*bag), me.member->account_id, me.member->room_id), *cached_content());
    if (!error.empty()) {
        return Err(error);
    }
    bag->club_in_hand = club_id;
    ctx.db[player_bag_account_id].update(*bag);
    return Ok();
}

// My hand, natural shape (game/golfer.h) and favourite colour
// (game/gear_paint.h) as a golfer, as my settings have them.
SPACETIMEDB_REDUCER(set_golfer, ReducerContext ctx, std::uint8_t hand, std::uint8_t natural, std::uint8_t colour) {
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
    me.player->favourite_colour = colour;
    ctx.db[player_account_id].update(*me.player);
    return Ok();
}

// Drops a practice ball at my feet in the hub (game/hub_ball.h), in place of
// my last one wherever it lies, as request_practice_drop offline.
SPACETIMEDB_REDUCER(place_practice_ball, ReducerContext ctx, std::uint64_t expected_room, float x, float y, float z) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const auto course = member_course(ctx, *me.member);
    const auto mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!course || !mover) { return Err(error_not_in_room); }
    const std::optional<ball_row> own = ctx.db[ball_account_id].find(me.member->account_id);
    const auto dropped_at = practice_drop_position(course->area, course->kind, position_of(*mover), {x, y, z},
        cached_content()->tuning);
    if (!dropped_at) { return Err(error_not_available); }
    const auto& point = *dropped_at;
    const ball_row dropped{me.member->account_id, expected_room, -1, point.x, point.y, point.z, 0, 0,
        ctx.timestamp, false, static_cast<std::uint8_t>(ball_kind::practice), -1};
    if (own) { ctx.db[ball_account_id].update(dropped); } else { ctx.db[ball].insert(dropped); }
    return Ok();
}

// Picks the holed ball out of its cup, in reach of the last accepted position
// (player.ball_interact_radius), as pick_up_cup_ball offline. Ok with no
// ball out: nothing is left to pick up.
SPACETIMEDB_REDUCER(pick_up_ball, ReducerContext ctx, std::uint64_t expected_room) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const std::optional<ball_row> in_cup = ctx.db[ball_account_id].find(me.member->account_id);
    if (!in_cup) {
        return Ok();
    }
    if (!in_cup->in_cup && in_cup->kind != static_cast<std::uint8_t>(ball_kind::practice)) {
        return Err(error_ball_in_play);
    }
    const server_content& content = *cached_content();
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (in_cup->kind == static_cast<std::uint8_t>(ball_kind::practice) && (!mover ||
        !practice_pickup_in_reach(ball_kind::practice, shot_has_played(*in_cup, ctx.timestamp, content.tuning.server),
            !in_cart(static_cast<motion_mode>(mover->mode)) && !at_ball(static_cast<motion_mode>(mover->mode)),
            position_of(*mover), {in_cup->x, in_cup->y, in_cup->z}, content.tuning))) { return Err(error_too_far); }

    if (!mover || !within_interact_reach(position_of(*mover), glm::vec3(in_cup->x, in_cup->y, in_cup->z),
                                          content.tuning.player.ball_interact_radius, content.tuning.server)) {
        return Err(error_too_far);
    }
    ctx.db[ball_account_id].delete_by_key(me.member->account_id);
    return Ok();
}

#include "dap_reducers.h"

// Uses an item the caller has equipped (what they smoke or drink), as
// offline: its XP per use and its timed effects, by the server's clock; not
// again while a use plays. Everyone in the room sees it.
SPACETIMEDB_REDUCER(use_item, ReducerContext ctx, std::uint64_t expected_room, std::string item_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    const std::uint64_t account = me.member->account_id;
    const server_content& content = *cached_content();
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
        const golfer_progress_row owner = permanent_golfer(ctx, account);
        const save_data before = load_progress(ctx, owner);
        store_progress(ctx, owner, before, award_xp(before, *item.use->xp_per_use, account_xp_effects(ctx, account)),
                       award_event{award_source_item, "item:" + std::to_string(owner.golfer_id) + ":" + item_id + ":" +
                                                          std::to_string(now)});
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
SPACETIMEDB_REDUCER_NAMED(claim_collectible_reducer, "claim_collectible", ReducerContext ctx, std::uint64_t expected_room, std::string collectible_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
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
    const std::optional<motion_state_row> mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!mover || !within_interact_reach(position_of(*mover), found->position, found->interaction_radius,
                                          cached_content()->tuning.server)) {
        return Err(error_too_far);
    }
    const golfer_progress_row owner = permanent_golfer(ctx, me.member->account_id);
    const save_data before = load_progress(ctx, owner);
    const claim_update claim =
        claim_collectible(before, *found, cached_content()->skills, account_xp_effects(ctx, me.member->account_id));
    if (!claim.claimed) {
        return Err(error_not_available);
    }
    // A one-off claim pays once ever; a repeatable one once per claim.
    std::string key = "collectible:" + std::to_string(owner.golfer_id) + ":" + found->id;
    if (found->repeatable) {
        key += ":" + std::to_string(claim.update.progress.repeatable_collectibles.at(found->id).claim_count);
    }
    store_progress(ctx, owner, before, claim.update, award_event{award_source_collectible, key});
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

SPACETIMEDB_VIEW(std::optional<server_content_info>, server_content_hash, Public, AnonymousViewContext ctx) {
    (void)ctx;
    const server_content* content = cached_content();
    return content ? std::optional<server_content_info>({content->content_hash}) : std::nullopt;
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

// The caller's permanent golfer and its progress rows.

namespace {
std::optional<golfer_progress_row> viewer_golfer(const ViewContext& ctx) {
    const std::optional<account_login_row> login = ctx.db[account_login_identity].find(ctx.sender());
    return login ? golfer_in(ctx, login->account_id, permanent_scope) : std::nullopt;
}

// `table`'s rows of the caller's permanent golfer.
template <typename Row, typename ByGolfer>
std::vector<Row> viewer_rows(const ViewContext& ctx, ByGolfer&& by_golfer) {
    std::vector<Row> rows;
    if (const std::optional<golfer_progress_row> mine = viewer_golfer(ctx)) {
        for (const Row& row : by_golfer.filter(mine->golfer_id)) {
            rows.push_back(row);
        }
    }
    return rows;
}
}

SPACETIMEDB_VIEW(std::optional<golfer_progress_row>, my_golfer_progress, Public, ViewContext ctx) {
    return viewer_golfer(ctx);
}

SPACETIMEDB_VIEW(std::vector<golfer_skill_row>, my_skills, Public, ViewContext ctx) {
    return viewer_rows<golfer_skill_row>(ctx, ctx.db[golfer_skill_golfer_id]);
}

SPACETIMEDB_VIEW(std::vector<completed_course_row>, my_completed_courses, Public, ViewContext ctx) {
    return viewer_rows<completed_course_row>(ctx, ctx.db[completed_course_golfer_id]);
}

SPACETIMEDB_VIEW(std::vector<collected_row>, my_collected, Public, ViewContext ctx) {
    return viewer_rows<collected_row>(ctx, ctx.db[collected_golfer_id]);
}

SPACETIMEDB_VIEW(std::vector<world_flag_row>, my_world_flags, Public, ViewContext ctx) {
    return viewer_rows<world_flag_row>(ctx, ctx.db[world_flag_golfer_id]);
}

SPACETIMEDB_VIEW(std::optional<link_status>, my_link_status, Public, ViewContext ctx) {
    const std::optional<link_attempt_row> attempt = ctx.db[link_attempt_identity].find(ctx.sender());
    return attempt ? std::optional<link_status>(link_status{attempt->last_result, attempt->last_at}) : std::nullopt;
}


namespace {
// The group and occupied seats stay visible even across distant areas.
std::set<std::uint64_t> retained_motion_accounts(const ViewContext& ctx, const room_member_row& member) {
    return ::retained_motion_accounts(member.account_id, member.room_id, member.group_id,
        [&](const std::uint64_t group) { return ctx.db[room_member_group_id].filter(group); },
        [&](const std::uint64_t account) { return ctx.db[golf_cart_owner].find(account); },
        [&](const std::uint64_t account) { return ctx.db[golf_cart_passenger].filter(account); });
}

std::optional<room_member_row> viewer_member(const ViewContext& ctx) {
    const auto login = ctx.db[account_login_identity].find(ctx.sender());
    return login ? ctx.db[room_member_account_id].find(login->account_id) : std::nullopt;
}
}

// Stable subscriptions: changes to area, group or seats change the view's
// rows in the same transaction. Deletions mean visibility ended, not a leave.
SPACETIMEDB_VIEW(std::vector<avatar_motion_row>, my_avatar_motion, Public, ViewContext ctx) {
    const auto member = viewer_member(ctx);
    if (!member) { return std::vector<avatar_motion_row>{}; }
    const auto own = ctx.db[avatar_motion_account_id].find(member->account_id);
    if (!own) { return std::vector<avatar_motion_row>{}; }
    return visible_motion_rows<avatar_motion_row>(member->room_id, motion_cell{own->area_x, own->area_z}, retained_motion_accounts(ctx, *member),
        [&](const motion_cell area) { return ctx.db[avatar_motion_area_key].filter(motion_area_key(member->room_id, area)); },
        [&](const std::uint64_t account) { return ctx.db[avatar_motion_account_id].find(account); },
        [](const avatar_motion_row& row) { return row.account_id; });
}

SPACETIMEDB_VIEW(std::vector<golf_cart_row>, my_golf_carts, Public, ViewContext ctx) {
    const auto member = viewer_member(ctx);
    if (!member) { return std::vector<golf_cart_row>{}; }
    const auto own = ctx.db[avatar_motion_account_id].find(member->account_id);
    if (!own) { return std::vector<golf_cart_row>{}; }
    return visible_motion_rows<golf_cart_row>(member->room_id, motion_cell{own->area_x, own->area_z}, retained_motion_accounts(ctx, *member),
        [&](const motion_cell area) { return ctx.db[golf_cart_area_key].filter(motion_area_key(member->room_id, area)); },
        [&](const std::uint64_t account) { return ctx.db[golf_cart_owner].find(account); },
        [](const golf_cart_row& row) { return row.owner; });
}
