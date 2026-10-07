#pragma once

// What game code and the network client (src/net/) share, so neither depends
// on the other. Game code pushes net_commands and reads online_view, the way
// it pushes audio_events: it never talks to the network itself.

#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// Picked from the menu, fixed until the player returns to it. Offline uses
// the local save; online uses progress from the server. They never mix.
enum class play_mode {
    offline,
    online
};

// The zone of a player who is walking the course rather than playing a hole.
inline constexpr int hub_zone = -1;

enum class motion_mode {
    idle,
    walk,
    cart,
    drift,
    aim,      // lining up a shot at their ball
    address,  // stood at the ball, club down
    swing     // the swing meter running: since this motion's server time
};

// Whether a player in `mode` is at their ball setting up a shot.
inline bool at_ball(const motion_mode mode) {
    return mode == motion_mode::aim || mode == motion_mode::address || mode == motion_mode::swing;
}

// Where a player is and how they are moving: enough for others to carry the
// movement on between updates (extrapolate_motion).
struct net_motion {
    int zone = hub_zone;  // hub_zone, else the hole index
    motion_mode mode = motion_mode::idle;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float speed = 0.0f;      // world units/s along yaw; negative walks backwards
    float turn_rate = 0.0f;  // rad/s, positive turns towards +X like yaw
    // Not sent: getting on or off a cart road changes the intent, so the
    // server judges each stretch's road XP on one kind of ground.
    bool on_road = false;
};

// Where `motion` carries a player after `seconds` at its speed and turn
// rate, keeping its height. Clients draw others with it; the server pays
// movement XP for the path it traces.
inline glm::vec3 extrapolate_motion(const net_motion& motion, const float seconds) {
    if (std::abs(motion.turn_rate) < 0.0001f) {
        return motion.position + yaw_direction(motion.yaw) * (motion.speed * seconds);
    }
    // The arc a constant speed and turn rate trace from yaw to yaw + turn.
    const float radius = motion.speed / motion.turn_rate;
    const float start = motion.yaw;
    const float end = motion.yaw + motion.turn_rate * seconds;
    return motion.position + glm::vec3(std::cos(start) - std::cos(end), 0.0f, std::sin(end) - std::sin(start)) * radius;
}

enum class emote_id {
    smoke,
    drink
};

// An emote's name on the wire ("smoke"), which the server checks.
inline const char* emote_name(const emote_id emote) {
    return emote == emote_id::smoke ? "smoke" : "drink";
}

inline std::optional<emote_id> emote_from_name(const std::string& name) {
    for (const emote_id emote : {emote_id::smoke, emote_id::drink}) {
        if (name == emote_name(emote)) {
            return emote;
        }
    }
    return std::nullopt;
}

enum class net_command_type {
    motion,
    take_shot,
    enter_hole,
    claim_collectible,
    emote,
    retee,
    pick_up_ball,
    create_group,
    join_group,
    leave_group,
    return_to_hub
};

// One outbound intent. Only the fields named for its type are meaningful.
struct net_command {
    net_command_type type = net_command_type::motion;
    net_motion motion;           // motion
    shot_input shot;             // take_shot
    int stroke = 0;              // take_shot: the stroke this shot is, from 1
    std::size_t hole_index = 0;  // enter_hole
    std::string collectible_id;  // claim_collectible
    emote_id emote = emote_id::smoke;  // emote
    std::uint64_t group_id = 0;  // join_group
};

// Where the connection to the server is.
enum class net_status {
    signed_out,
    signing_in,
    waiting_for_browser,
    connecting,
    connected,
    failed  // see net_client::failure()
};

// The failure ids the bridge itself gives (src/net/stdb_bridge.h), besides
// the server's (server_errors.h). One per line: a test reads them.
inline constexpr const char* net_failure_not_connected = "not_connected";
inline constexpr const char* net_failure_sign_in_cancelled = "sign_in_cancelled";
inline constexpr const char* net_failure_sign_in_timed_out = "sign_in_timed_out";
inline constexpr const char* net_failure_browser_failed = "browser_failed";
inline constexpr const char* net_failure_sign_in_failed = "sign_in_failed";
inline constexpr const char* net_failure_no_stored_sign_in = "no_stored_sign_in";
inline constexpr const char* net_failure_connect_failed = "connect_failed";
inline constexpr const char* net_failure_connection_rejected = "connection_rejected";
inline constexpr const char* net_failure_connection_lost = "connection_lost";
inline constexpr const char* net_failure_subscription_failed = "subscription_failed";
inline constexpr const char* net_failure_request_failed = "request_failed";

// A reducer the server refused, with its server_errors.h id.
struct reducer_failure {
    std::string reducer;
    std::string error;
};

// What redeem_link_code did (the my_link_status view). One per line: a test
// reads them.
inline constexpr const char* link_result_linked = "linked";
inline constexpr const char* link_result_invalid_code = "invalid_code";
inline constexpr const char* link_result_too_many_attempts = "too_many_attempts";
inline constexpr const char* link_result_same_account = "same_account";
inline constexpr const char* link_result_has_progress = "has_progress";

// Link codes: what create_link_code makes and redeem_link_code takes.
inline constexpr int link_code_length = 8;
// No 0/O or 1/I, which are easy to mix up when typed from the screen.
inline constexpr const char* link_code_alphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZ";

// The room I am in: one course, shared with up to the server's room_capacity.
struct online_room {
    std::uint64_t room_id = 0;
    std::string course_id;
    int player_count = 0;
};

// Server times are microseconds since the Unix epoch, on the server's clock.
inline float server_seconds_between(const std::int64_t earlier, const std::int64_t later) {
    return static_cast<float>(static_cast<double>(later - earlier) / 1000000.0);
}

inline std::int64_t server_micros(const double seconds) {
    return static_cast<std::int64_t>(seconds * 1000000.0);
}

// A player in my room, me included: their room_member, player and
// avatar_motion rows.
struct room_player {
    std::string name;
    std::uint64_t group_id = 0;  // 0: none
    int zone = hub_zone;
    std::int64_t hole_started_at = 0;  // when the hole was entered or reteed: wind time starts here
    std::vector<int> round_strokes;    // per hole this round, 0: not played
    // Where the server last accepted them, and when (server time).
    std::optional<net_motion> motion;
    std::int64_t motion_at = 0;
};

// A player's ball on the hole they play (the ball row).
struct room_ball {
    int zone = hub_zone;
    glm::vec3 position{0.0f};
    int stroke_count = 0;
};

// A tee is in use while another player's ball waits on it: they started that
// hole and have not hit their tee shot. Nobody else tees up there until then:
// the server's enter_hole refuses it, and the client before asking.
inline bool tee_in_use(const std::map<std::uint64_t, room_ball>& balls, const std::uint64_t me, const int hole) {
    for (const auto& [account_id, ball] : balls) {
        if (account_id != me && ball.zone == hole && ball.stroke_count == 0) {
            return true;
        }
    }
    return false;
}

// A shot the server played (a shot_event row): every client plays it from
// these inputs, and it rests where the server says.
struct room_shot {
    std::uint64_t account_id = 0;
    int zone = hub_zone;
    int stroke = 0;
    shot_input input;
    glm::vec3 rest{0.0f};
    bool holed = false;
};

// Another player's emote the server accepted (an emote_event row).
struct room_emote {
    std::uint64_t account_id = 0;
    emote_id emote = emote_id::smoke;
};

// The latest server data the game reads in online mode. Only ever filled from
// the server (net_client, and receive_online_progress in game/mode_dispatch.h).
struct online_view {
    net_status status = net_status::signed_out;
    std::uint64_t account_id = 0;  // 0 until my account arrives
    std::string display_name;      // empty until claimed
    std::optional<online_room> room;
    // Everyone in my room by account id, me included; their balls; how many
    // players each group has.
    std::map<std::uint64_t, room_player> players;
    std::map<std::uint64_t, room_ball> balls;
    std::map<std::uint64_t, int> group_sizes;
    // Shots, others' emotes and refused gameplay reducers that arrived and
    // the game has not taken yet, oldest first.
    std::vector<room_shot> shots;
    std::vector<room_emote> emotes;
    std::vector<reducer_failure> refusals;
    // The server's clock now, as near as this client can tell (from its own
    // avatar_motion rows); 0 until known.
    std::int64_t server_now = 0;
    // When the server accepted my last smoke emote: the cigarette is lit
    // from then (server time; 0: never).
    std::int64_t smoke_accepted_at = 0;
    save_data progress;  // my online progress, shaped like the offline save
    bool progress_received = false;  // false until the server's first snapshot
    // Collectibles claimed but not answered yet, oldest first: not offered
    // again until the progress shows them claimed or the claim is refused.
    std::vector<std::string> pending_claims;
};
