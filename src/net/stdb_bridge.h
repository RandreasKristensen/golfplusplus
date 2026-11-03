#pragma once

// The C API of golf++'s connection to its SpacetimeDB server, implemented in
// Rust (net/client_bridge, whose src/ffi.rs declares the same types: keep
// both in the same order and layout). Plain data only. Strings are UTF-8
// with a length, not NUL-terminated; strings the bridge hands out stay valid
// until the next stdb_poll. Everything runs on the thread that calls
// stdb_frame_tick, and no call blocks. Only src/net/ includes this.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct stdb_client stdb_client;

typedef struct stdb_string {
    const char* data;
    size_t len;
} stdb_string;

typedef struct stdb_config {
    stdb_string server_uri;
    stdb_string database;
    stdb_string auth_issuer;
    stdb_string auth_client_id;
    stdb_string auth_scopes;
    stdb_string auth_authorization_endpoint;
    stdb_string auth_token_endpoint;
    // What the browser shows after signing in, and after a sign-in that did
    // not finish (from the game's string table).
    stdb_string page_signed_in;
    stdb_string page_failed;
    bool anonymous;  // connect without signing in (servers with allow_anonymous only)
} stdb_config;

typedef enum stdb_login_method {
    STDB_LOGIN_BROWSER = 0,
    STDB_LOGIN_ANONYMOUS = 1
} stdb_login_method;

typedef enum stdb_event_kind {
    STDB_EVENT_LOGIN_WAITING_FOR_BROWSER = 0,
    STDB_EVENT_LOGIN_FAILED = 1,
    STDB_EVENT_SIGNED_IN = 2,
    STDB_EVENT_CONNECTED = 3,
    STDB_EVENT_DISCONNECTED = 4,
    STDB_EVENT_SUBSCRIPTION_APPLIED = 5,
    STDB_EVENT_SUBSCRIPTION_FAILED = 6,
    STDB_EVENT_ROW = 7,
    STDB_EVENT_REDUCER_FAILED = 8
} stdb_event_kind;

typedef enum stdb_table {
    STDB_TABLE_PLAYER = 0,
    STDB_TABLE_MY_SKILLS = 1,
    STDB_TABLE_MY_GOLFER_PROGRESS = 2,
    STDB_TABLE_MY_COMPLETED_COURSES = 3,
    STDB_TABLE_MY_COLLECTED = 4,
    STDB_TABLE_MY_WORLD_FLAGS = 5,
    STDB_TABLE_ROOM = 6,
    STDB_TABLE_ROOM_MEMBER = 7,
    STDB_TABLE_GOLF_GROUP = 8,
    STDB_TABLE_AVATAR_MOTION = 9,
    STDB_TABLE_BALL = 10,
    STDB_TABLE_SHOT_EVENT = 11,
    STDB_TABLE_ITEM_USE_EVENT = 12,
    STDB_TABLE_MY_ACCOUNT = 13,
    STDB_TABLE_MY_LINK_CODE = 14,
    STDB_TABLE_MY_LINK_STATUS = 15,
    STDB_TABLE_SERVER_PROTOCOL = 16,
    STDB_TABLE_GOLF_CART = 17,
    STDB_TABLE_PLAYER_LOADOUT = 18,
    STDB_TABLE_RUNNING_EFFECT = 19,
    STDB_TABLE_PLAYER_BAG = 20,
    STDB_TABLE_GREEN_FLAG = 21,
    STDB_TABLE_RIDE_MOTION = 22,
    STDB_TABLE_SERVER_CONTENT_HASH = 23
} stdb_table;

typedef enum stdb_row_change {
    STDB_ROW_INSERT = 0,
    STDB_ROW_UPDATE = 1,  // the row's new value; rows are keyed by their primary key
    STDB_ROW_DELETE = 2
} stdb_row_change;

typedef struct stdb_player {  // also my_account
    uint64_t account_id;
    stdb_string display_name;
    bool online;
    uint8_t hand;           // handedness in game/shot_setup.h
    uint8_t natural_shape;  // natural_shape in game/golfer.h, or natural_shape_unset
    uint8_t favourite_colour;  // game/gear_paint.h, 0 for none
} stdb_player;

// My progress, through the my_* views: my permanent golfer's.
typedef struct stdb_golfer_skill {  // my_skills
    uint64_t id;
    uint64_t golfer_id;
    stdb_string skill_id;
    int64_t xp;
} stdb_golfer_skill;

typedef struct stdb_golfer_progress {  // my_golfer_progress
    uint64_t golfer_id;
    uint64_t account_id;
    uint32_t scope_id;
    int32_t holes_completed;
} stdb_golfer_progress;

typedef struct stdb_completed_course {  // my_completed_courses
    uint64_t id;
    uint64_t golfer_id;
    stdb_string course_id;
} stdb_completed_course;

typedef struct stdb_collected {  // my_collected
    uint64_t id;
    uint64_t golfer_id;
    stdb_string collectible_id;
    bool repeatable;
    int32_t claim_count;
    int32_t claimed_at_holes_completed;  // -1: never
} stdb_collected;

typedef struct stdb_world_flag {  // my_world_flags
    uint64_t id;
    uint64_t golfer_id;
    stdb_string flag;
} stdb_world_flag;

typedef struct stdb_room {
    uint64_t room_id;
    stdb_string course_id;
    int32_t player_count;
} stdb_room;

typedef struct stdb_room_member {
    uint64_t account_id;
    uint64_t room_id;
    uint64_t group_id;  // 0: none
    const int32_t* round_strokes;  // per hole this round, 0: not played
    size_t round_strokes_count;
} stdb_room_member;

typedef struct stdb_golf_group {
    uint64_t group_id;
    uint64_t room_id;
    uint64_t leader;
    int32_t member_count;
} stdb_golf_group;

// A setup at address as sent (game/sent_setup.h).
typedef struct stdb_setup {
    uint16_t aim;
    int16_t face_open;
    int8_t ball_position;
    uint16_t backswing;
} stdb_setup;

typedef struct stdb_motion {
    uint8_t mode;  // motion_mode in game/net_types.h
    float x;
    float y;
    float z;
    float yaw;
    float speed;
    float turn_rate;
    double client_time;
    stdb_setup setup;  // avatar motion only
    bool crouched;     // avatar motion only
} stdb_motion;

// A run of fixed steps with the same input (game/movement_step.h move_run).
typedef struct stdb_move_run {
    uint8_t kind;  // move_kind
    uint16_t steps;
    int8_t forward;
    int8_t side;
    bool run;
    bool jump;
    uint16_t yaw;
    int16_t steer;
} stdb_move_run;

// The fixed steps a motion carries: numbered on from first_step, the first
// on foot taken from start_*.
typedef struct stdb_steps {
    uint64_t first_step;
    float start_x;
    float start_y;
    float start_z;
    const stdb_move_run* runs;
    size_t run_count;
} stdb_steps;

typedef struct stdb_avatar_motion {
    uint64_t account_id;
    uint64_t room_id;
    stdb_motion motion;
    int64_t server_time_micros;
} stdb_avatar_motion;

typedef struct stdb_ride_motion {
    uint64_t rider;
    uint64_t owner;
    stdb_motion motion;
    int64_t server_time_micros;
} stdb_ride_motion;

typedef struct stdb_ball {
    uint64_t account_id;
    uint64_t room_id;
    int32_t hole;
    float x;
    float y;
    float z;
    int32_t stroke_count;
    int64_t last_shot_at;  // UTC milliseconds
    bool in_cup;
} stdb_ball;

typedef struct stdb_golf_cart {
    uint64_t owner;
    uint64_t room_id;
    float x;
    float y;
    float z;
    float yaw;
    uint64_t passenger;  // 0: nobody
    uint8_t condition;   // cart_condition (physics/cart_dynamics.h)
    uint64_t driver;
    double motion_id;
} stdb_golf_cart;

// A setup to play (game/shot_setup.h), and the holes whose flag the
// client has out.
typedef struct stdb_shot {
    int32_t stroke;
    float aim_angle;
    float face_open;
    float ball_position;
    float backswing;
    float strike_timing;
    stdb_string club_id;
    int64_t shot_at;  // UTC milliseconds, on the server's clock as the client estimates it
    const int32_t* flags_out;
    size_t flags_out_count;
} stdb_shot;

// What moved that a shot could meet (game/shot_snapshot.h snapshot_entry).
typedef struct stdb_snapshot_entry {
    uint8_t kind;  // snapshot_kind
    float x;
    float y;
    float z;
    float yaw;
    bool shooter;
} stdb_snapshot_entry;

// A golfer's level in a skill, as a shot used it.
typedef struct stdb_skill_level {
    stdb_string skill_id;
    int32_t level;
} stdb_skill_level;

typedef struct stdb_shot_event {
    uint64_t room_id;
    uint64_t account_id;
    int32_t hole;
    stdb_shot shot;
    uint8_t hand;           // handedness
    uint8_t natural_shape;  // the golfer's as the server played it
    float spread_scale;     // likewise
    const stdb_skill_level* levels;  // likewise, before the shot
    size_t levels_count;
    uint64_t seed;
    const stdb_snapshot_entry* snapshot;
    size_t snapshot_count;
    float start_x;
    float start_y;
    float start_z;
    float rest_x;
    float rest_y;
    float rest_z;
    bool holed;
} stdb_shot_event;

typedef struct stdb_item_use_event {
    uint64_t room_id;
    uint64_t account_id;
    stdb_string item_id;
} stdb_item_use_event;

typedef struct stdb_loadout_mount {
    stdb_string mount;
    stdb_string item_id;
} stdb_loadout_mount;

typedef struct stdb_player_loadout {
    uint64_t account_id;
    stdb_string cart;
    const stdb_loadout_mount* cart_mounts;
    size_t cart_mounts_count;
    stdb_string smokable;
    stdb_string drinkable;
    stdb_string bag;
} stdb_player_loadout;

typedef struct stdb_player_bag {
    uint64_t account_id;
    uint64_t room_id;
    uint8_t place;  // bag_where in game/bag_place.h
    uint8_t seat;   // cart_seat
    uint64_t cart_owner;  // whose cart, 0 their own
    float x;
    float y;
    float z;
    float yaw;
    stdb_string club_in_hand;
} stdb_player_bag;

typedef struct stdb_green_flag {
    uint64_t id;
    uint64_t room_id;
    int32_t hole;
} stdb_green_flag;

typedef struct stdb_running_effect {
    uint64_t id;
    uint64_t account_id;
    stdb_string item_id;
    stdb_string target;
    uint8_t bucket;    // effect_bucket in game/effects.h
    float value;
    uint8_t duration;  // effect_duration
    float seconds;
    int64_t started_at_ms;  // UTC milliseconds
    int64_t ends_at_ms;
} stdb_running_effect;

typedef struct stdb_link_code {
    stdb_string code;
    int64_t expires_at_micros;
} stdb_link_code;

typedef struct stdb_link_status {
    stdb_string result;
    int64_t at_micros;
} stdb_link_status;

typedef struct stdb_server_protocol {
    uint32_t version;  // protocol_version in game/net_types.h
} stdb_server_protocol;

typedef struct stdb_server_content {
    uint64_t hash;
} stdb_server_content;

typedef union stdb_row {
    stdb_player player;
    stdb_golfer_skill golfer_skill;
    stdb_golfer_progress golfer_progress;
    stdb_completed_course completed_course;
    stdb_collected collected;
    stdb_world_flag world_flag;
    stdb_room room;
    stdb_room_member room_member;
    stdb_golf_group golf_group;
    stdb_avatar_motion avatar_motion;
    stdb_ball ball;
    stdb_shot_event shot_event;
    stdb_item_use_event item_use_event;
    stdb_link_code link_code;
    stdb_link_status link_status;
    stdb_server_protocol server_protocol;
    stdb_server_content server_content;
    stdb_golf_cart golf_cart;
    stdb_player_loadout player_loadout;
    stdb_running_effect running_effect;
    stdb_player_bag player_bag;
    stdb_green_flag green_flag;
    stdb_ride_motion ride_motion;
} stdb_row;

// One thing that happened. Only the fields named for its kind are set.
//
// LOGIN_FAILED and DISCONNECTED say why as "id: detail", the id one of:
// sign_in_cancelled, sign_in_timed_out, browser_failed, sign_in_failed,
// no_stored_sign_in, connect_failed, connection_rejected (the server refused
// the login), connection_lost. A DISCONNECTED the game asked for has no text.
// SUBSCRIPTION_FAILED says "subscription_failed: detail"; REDUCER_FAILED the
// server's id, or "request_failed: detail" / "not_connected" from the bridge.
typedef struct stdb_event {
    stdb_event_kind kind;
    // LOGIN_FAILED, DISCONNECTED, SUBSCRIPTION_FAILED: why (above).
    // REDUCER_FAILED: the server's error id (server_errors.h), or the bridge's.
    // CONNECTED: the identity, in hex.
    stdb_string text;
    stdb_string reducer;            // REDUCER_FAILED: which
    uint32_t subscription;          // SUBSCRIPTION_*: the stdb_subscribe handle
    stdb_login_method login_method; // SIGNED_IN
    bool retrying;                  // DISCONNECTED: the bridge signs in again by itself
    stdb_table table;               // ROW
    stdb_row_change change;         // ROW
    stdb_row row;                   // ROW
} stdb_event;

// Every struct's size and field offset as the bridge lays them out, in the
// order net_client's layout check lists them. Writes up to `max` values and
// returns how many there are.
size_t stdb_layout(size_t* out, size_t max);

stdb_client* stdb_create(const stdb_config* config);
void stdb_destroy(stdb_client* client);

// Signs in and connects: anonymously when configured so, else silently with
// a stored sign-in or (unless `silent_only`) through the system browser.
// False when already signing in or connected.
bool stdb_begin_login(stdb_client* client, bool silent_only);
void stdb_cancel_login(stdb_client* client);
// Disconnects, keeping the stored sign-in.
void stdb_disconnect(stdb_client* client);
// Disconnects and forgets the stored sign-in.
void stdb_sign_out(stdb_client* client);

// Advances sign-in and the connection and runs its callbacks. Returns 1 while
// connected, else 0.
int32_t stdb_frame_tick(stdb_client* client);
// Copies up to `max` events into `out`; returns how many.
size_t stdb_poll(stdb_client* client, stdb_event* out, size_t max);

// Subscribes to SQL queries. Returns a handle (0 when not connected);
// SUBSCRIPTION_APPLIED or SUBSCRIPTION_FAILED follows with it.
uint32_t stdb_subscribe(stdb_client* client, const stdb_string* queries, size_t count);
void stdb_unsubscribe(stdb_client* client, uint32_t subscription);

// Reducers. A failure arrives as REDUCER_FAILED.
void stdb_claim_name(stdb_client* client, const char* name, size_t len);
void stdb_join_course(stdb_client* client, const char* course_id, size_t len, bool takes_cart);
void stdb_leave_room(stdb_client* client);
void stdb_create_group(stdb_client* client);
void stdb_join_group(stdb_client* client, uint64_t group_id);
void stdb_leave_group(stdb_client* client);
// False if the SDK could not enqueue it; motion remains due in the game.
bool stdb_update_motion(stdb_client* client, const stdb_motion* motion, const stdb_steps* steps);
void stdb_update_ride(stdb_client* client, const stdb_motion* motion);
void stdb_tee_up(stdb_client* client, int32_t hole_index, bool give_up);
void stdb_give_up_hole(stdb_client* client);
void stdb_pick_up_ball(stdb_client* client);
void stdb_take_wheel(stdb_client* client);
void stdb_leave_wheel(stdb_client* client);
void stdb_board_cart(stdb_client* client, uint64_t owner);
void stdb_leave_cart(stdb_client* client);
void stdb_right_cart(stdb_client* client, uint64_t owner);
void stdb_take_shot(stdb_client* client, const stdb_shot* shot);
void stdb_set_flag(stdb_client* client, int32_t hole, bool out);
void stdb_place_bag(stdb_client* client, uint8_t place, uint8_t seat, uint64_t cart_owner, float x, float y, float z, float yaw);
void stdb_take_club(stdb_client* client, const char* club_id, size_t len);
void stdb_set_golfer(stdb_client* client, uint8_t hand, uint8_t natural_shape, uint8_t favourite_colour);
void stdb_use_item(stdb_client* client, const char* item_id, size_t len);
void stdb_claim_collectible(stdb_client* client, const char* collectible_id, size_t len);
void stdb_create_link_code(stdb_client* client);
void stdb_redeem_link_code(stdb_client* client, const char* code, size_t len);

#ifdef __cplusplus
}
#endif
