#pragma once

// What game code and the network client (src/net/) share, so neither depends
// on the other. Game code pushes net_commands and reads online_view, the way
// it pushes audio_events: it never talks to the network itself.

#include "game/bag_place.h"
#include "game/effects.h"
#include "game/golfer.h"
#include "game/loadout.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "physics/cart_dynamics.h"
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

enum class motion_mode {
    idle,
    walk,
    cart,     // at the wheel of their own cart
    ride,     // in the passenger seat of another player's cart
    address,  // stood over their ball, setting the shot up
    swing     // swinging: since this motion's server time
};

// Whether a player in `mode` is at their ball setting up a shot.
inline bool at_ball(const motion_mode mode) {
    return mode == motion_mode::address || mode == motion_mode::swing;
}

// Whether a player in `mode` is sat in a cart, driving it or riding along.
inline bool in_cart(const motion_mode mode) {
    return mode == motion_mode::cart || mode == motion_mode::ride;
}

// Where a player is and how they are moving: enough for others to carry the
// movement on between updates (extrapolate_motion).
struct net_motion {
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

enum class net_command_type {
    motion,
    take_shot,
    tee_up,
    claim_collectible,
    use_item,
    pick_up_ball,
    create_group,
    join_group,
    leave_group,
    give_up_hole,
    board_cart,
    leave_cart,
    overturn_cart,
    right_cart,
    set_flag,
    place_bag,
    take_club,
    set_golfer
};

// The server's reducers that net_commands call, by name: a refusal
// (reducer_failure) names the one it came from.
inline constexpr const char* reducer_update_motion = "update_motion";
inline constexpr const char* reducer_take_shot = "take_shot";
inline constexpr const char* reducer_tee_up = "tee_up";
inline constexpr const char* reducer_claim_collectible = "claim_collectible";
inline constexpr const char* reducer_use_item = "use_item";
inline constexpr const char* reducer_pick_up_ball = "pick_up_ball";
inline constexpr const char* reducer_give_up_hole = "give_up_hole";
inline constexpr const char* reducer_create_group = "create_group";
inline constexpr const char* reducer_join_group = "join_group";
inline constexpr const char* reducer_leave_group = "leave_group";
inline constexpr const char* reducer_board_cart = "board_cart";
inline constexpr const char* reducer_leave_cart = "leave_cart";
inline constexpr const char* reducer_overturn_cart = "overturn_cart";
inline constexpr const char* reducer_right_cart = "right_cart";
inline constexpr const char* reducer_set_flag = "set_flag";
inline constexpr const char* reducer_place_bag = "place_bag";
inline constexpr const char* reducer_take_club = "take_club";
inline constexpr const char* reducer_set_golfer = "set_golfer";

// One outbound intent. Only the fields named for its type are meaningful.
struct net_command {
    net_command_type type = net_command_type::motion;
    net_motion motion;           // motion
    shot_setup shot;             // take_shot
    int stroke = 0;              // take_shot: the stroke this shot is, from 1
    std::vector<int> flags_out;  // take_shot: the holes whose flag I have out
    std::size_t hole_index = 0;  // tee_up, set_flag
    bool flag_out = false;       // set_flag
    bag_place bag;               // place_bag: where to
    std::string club_id;         // take_club
    handedness hand = handedness::right;  // set_golfer
    std::uint8_t natural = natural_shape_unset;  // set_golfer
    bool give_up = false;        // tee_up: giving up the hole my ball is in play on
    std::string collectible_id;  // claim_collectible
    std::string item_id;         // use_item
    std::uint64_t group_id = 0;  // join_group
    std::uint64_t cart_owner = 0;  // board_cart: whose cart's passenger seat; right_cart: whose cart
    cart_condition overturned_to = cart_condition::upright;  // overturn_cart: on which side, or sunk
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

// The server's tables, views and reducers as this game knows them. The
// module reports its own (the server_protocol view) and the client plays
// only on a server with the same, else asks for an update
// (net_failure_outdated_game). Bump it with every change to the module's
// tables, views or reducers; the server_protocol view itself never changes.
inline constexpr std::uint32_t protocol_version = 7;

// The failure ids the bridge and the client give (src/net/stdb_bridge.h,
// net_client), besides the server's (server_errors.h). One per line: a test
// reads them.
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
inline constexpr const char* net_failure_outdated_game = "outdated_game";

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

// A player in my room, me included: their room_member, player and
// avatar_motion rows.
struct room_player {
    std::string name;
    // As a golfer (their player row): their hand and natural shape, unset until picked.
    handedness hand = handedness::right;
    std::uint8_t natural_shape = natural_shape_unset;
    std::uint64_t group_id = 0;  // 0: none
    std::vector<int> round_strokes;    // per hole this round, 0: not played
    // Where the server last accepted them, and when (server time).
    std::optional<net_motion> motion;
    std::int64_t motion_at = 0;
};

// A player's ball (the ball row): in play on its hole, or in its cup until
// they pick it up.
struct room_ball {
    int hole = 0;
    glm::vec3 position{0.0f};
    int stroke_count = 0;
    bool in_cup = false;
};

// A tee is in use while another player's ball waits on it: they teed up on
// that hole and have not hit their tee shot. Nobody else tees up there until
// then: the server's tee_up refuses it, and the client before asking.
inline bool tee_in_use(const std::map<std::uint64_t, room_ball>& balls, const std::uint64_t me, const int hole) {
    for (const auto& [account_id, ball] : balls) {
        if (account_id != me && ball.hole == hole && ball.stroke_count == 0 && !ball.in_cup) {
            return true;
        }
    }
    return false;
}

// A shot the server played (a shot_event row): every client plays it from
// these inputs (its record, the golfer as the server played them, the
// flags then), and it rests where the server says.
struct room_shot {
    std::uint64_t account_id = 0;
    int hole = 0;
    int stroke = 0;
    shot_record record;
    golfer_profile golfer;
    std::vector<int> flags_out;
    glm::vec3 rest{0.0f};
    bool holed = false;
};

// A player's bag in my room and the club in their hand (a player_bag row).
struct room_bag {
    bag_place place;
    std::string club_in_hand;
};

// A player's golf cart in my room (a golf_cart row), mine included: where
// it was parked or last driven (game/golf_cart.h cart_pose), and who rides
// in its passenger seat.
struct room_cart {
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    std::uint64_t passenger = 0;  // 0: nobody
    cart_condition condition = cart_condition::upright;
};

// A golf_cart row's condition as sent (cart_condition's number), or nullopt
// when it is none.
inline std::optional<cart_condition> cart_condition_from_number(const std::uint8_t number) {
    if (number > static_cast<std::uint8_t>(cart_condition::sunk)) {
        return std::nullopt;
    }
    return static_cast<cart_condition>(number);
}

// Another player's use of an item the server accepted (an item_use_event row).
struct room_item_use {
    std::uint64_t account_id = 0;
    std::string item_id;
};

// The latest server data the game reads in online mode. Only ever filled from
// the server (net_client, and receive_online_progress in game/mode_dispatch.h).
struct online_view {
    net_status status = net_status::signed_out;
    std::uint64_t account_id = 0;  // 0 until my account arrives
    std::string display_name;      // empty until claimed
    std::optional<online_room> room;
    // Everyone in my room by account id, me included; their balls; how many
    // players each group has; their carts.
    std::map<std::uint64_t, room_player> players;
    std::map<std::uint64_t, room_ball> balls;
    std::map<std::uint64_t, int> group_sizes;
    std::map<std::uint64_t, room_cart> carts;  // by owner
    std::map<std::uint64_t, loadout> loadouts;  // by account, as their player_loadout rows have it
    std::map<std::uint64_t, room_bag> bags;     // by account
    std::vector<int> flags_out;                 // the holes whose flag is out in my room
    // Shots, others' item uses and refused gameplay reducers that arrived
    // and the game has not taken yet, oldest first.
    std::vector<room_shot> shots;
    std::vector<room_item_use> item_uses;
    std::vector<reducer_failure> refusals;
    // The server's clock now, as near as this client can tell (from its own
    // avatar_motion rows); 0 until known.
    std::int64_t server_now = 0;
    // The timed effects of what I used, as the server runs them.
    std::vector<active_effect> effects;
    save_data progress;  // my online progress, shaped like the offline save
    bool progress_received = false;  // false until the server's first snapshot
    // Collectibles claimed but not answered yet, oldest first: not offered
    // again until the progress shows them claimed or the claim is refused.
    std::vector<std::string> pending_claims;
};
