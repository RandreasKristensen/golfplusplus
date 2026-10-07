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
    STDB_TABLE_PLAYER_SKILL = 1,
    STDB_TABLE_HOLE_SCORE = 2,
    STDB_TABLE_COMPLETED_COURSE = 3,
    STDB_TABLE_COLLECTED = 4,
    STDB_TABLE_WORLD_FLAG = 5,
    STDB_TABLE_ROOM = 6,
    STDB_TABLE_ROOM_MEMBER = 7,
    STDB_TABLE_GOLF_GROUP = 8,
    STDB_TABLE_AVATAR_MOTION = 9,
    STDB_TABLE_BALL = 10,
    STDB_TABLE_SHOT_EVENT = 11,
    STDB_TABLE_EMOTE_EVENT = 12,
    STDB_TABLE_MY_ACCOUNT = 13,
    STDB_TABLE_MY_LINK_CODE = 14,
    STDB_TABLE_MY_LINK_STATUS = 15
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
    int32_t holes_completed;
} stdb_player;

typedef struct stdb_player_skill {
    uint64_t id;
    uint64_t account_id;
    stdb_string skill_id;
    int32_t xp;
} stdb_player_skill;

typedef struct stdb_hole_score {
    uint64_t id;
    uint64_t account_id;
    stdb_string course_id;
    int32_t hole_index;
    int32_t strokes;
    int32_t par;
} stdb_hole_score;

typedef struct stdb_completed_course {
    uint64_t id;
    uint64_t account_id;
    stdb_string course_id;
} stdb_completed_course;

typedef struct stdb_collected {
    uint64_t id;
    uint64_t account_id;
    stdb_string collectible_id;
    bool repeatable;
    int32_t claim_count;
    int32_t claimed_at_holes_completed;  // -1: never
} stdb_collected;

typedef struct stdb_world_flag {
    uint64_t id;
    uint64_t account_id;
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
    int32_t zone;       // -1: the hub, else the hole played
    int64_t hole_started_at_micros;  // when the hole was entered or reteed (server clock): wind time starts here
    const int32_t* round_strokes;  // per hole this round, 0: not played
    size_t round_strokes_count;
} stdb_room_member;

typedef struct stdb_golf_group {
    uint64_t group_id;
    uint64_t room_id;
    uint64_t leader;
    int32_t member_count;
} stdb_golf_group;

typedef struct stdb_motion {
    int32_t zone;
    uint8_t mode;  // motion_mode in game/net_types.h
    float x;
    float y;
    float z;
    float yaw;
    float speed;
    float turn_rate;
    double client_time;
} stdb_motion;

typedef struct stdb_avatar_motion {
    uint64_t account_id;
    uint64_t room_id;
    stdb_motion motion;
    int64_t server_time_micros;
} stdb_avatar_motion;

typedef struct stdb_ball {
    uint64_t account_id;
    uint64_t room_id;
    int32_t zone;
    float x;
    float y;
    float z;
    int32_t stroke_count;
    float last_wind_time;
} stdb_ball;

typedef struct stdb_shot {
    int32_t stroke;
    float aim_angle;
    stdb_string club_id;
    float power;
    bool cigarette_active;
    float wind_time;
} stdb_shot;

typedef struct stdb_shot_event {
    uint64_t room_id;
    uint64_t account_id;
    int32_t zone;
    stdb_shot shot;
    float start_x;
    float start_y;
    float start_z;
    float rest_x;
    float rest_y;
    float rest_z;
    bool holed;
} stdb_shot_event;

typedef struct stdb_emote_event {
    uint64_t room_id;
    uint64_t account_id;
    stdb_string emote_id;
} stdb_emote_event;

typedef struct stdb_link_code {
    stdb_string code;
    int64_t expires_at_micros;
} stdb_link_code;

typedef struct stdb_link_status {
    stdb_string result;
    int64_t at_micros;
} stdb_link_status;

typedef union stdb_row {
    stdb_player player;
    stdb_player_skill player_skill;
    stdb_hole_score hole_score;
    stdb_completed_course completed_course;
    stdb_collected collected;
    stdb_world_flag world_flag;
    stdb_room room;
    stdb_room_member room_member;
    stdb_golf_group golf_group;
    stdb_avatar_motion avatar_motion;
    stdb_ball ball;
    stdb_shot_event shot_event;
    stdb_emote_event emote_event;
    stdb_link_code link_code;
    stdb_link_status link_status;
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
void stdb_join_course(stdb_client* client, const char* course_id, size_t len);
void stdb_leave_room(stdb_client* client);
void stdb_create_group(stdb_client* client);
void stdb_join_group(stdb_client* client, uint64_t group_id);
void stdb_leave_group(stdb_client* client);
void stdb_update_motion(stdb_client* client, const stdb_motion* motion);
void stdb_enter_hole(stdb_client* client, int32_t hole_index);
void stdb_return_to_hub(stdb_client* client);
void stdb_retee(stdb_client* client);
void stdb_pick_up_ball(stdb_client* client);
void stdb_take_shot(stdb_client* client, const stdb_shot* shot);
void stdb_emote(stdb_client* client, const char* emote_id, size_t len);
void stdb_claim_collectible(stdb_client* client, const char* collectible_id, size_t len);
void stdb_create_link_code(stdb_client* client);
void stdb_redeem_link_code(stdb_client* client, const char* code, size_t len);

#ifdef __cplusplus
}
#endif
