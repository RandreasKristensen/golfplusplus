#pragma once

// Rules for playing online, as plain functions the module's reducers call
// and the native tests check: which room a player joins, how far they may
// move and what that earns, what is in reach, and whether a shot is valid.
// The server decides shots and progress; movement comes from the client and
// is only checked against what the game allows. Errors are the ids in
// server_errors.h.

#include "game/bag_place.h"
#include "game/cart_seats.h"
#include "game/golfer.h"
#include "game/game_tuning.h"
#include "game/net_types.h"
#include "game/shot_simulation.h"
#include "physics/cart_dynamics.h"
#include "server_content.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

// --- Rooms ---------------------------------------------------------------------

struct room_candidate {
    std::uint64_t room_id = 0;
    int player_count = 0;
};

// The fullest room with space, so rooms fill before new ones open; the
// oldest (lowest id) among equals. nullopt when every room is full.
std::optional<std::uint64_t> choose_room(const std::vector<room_candidate>& rooms, int capacity);

// --- Movement ------------------------------------------------------------------

// Clients send motion_mode (game/net_types.h) as its underlying number; any
// below this is a valid mode.
inline constexpr std::uint8_t motion_mode_count = static_cast<std::uint8_t>(motion_mode::swing) + 1;

// How fast the game moves a player: walking speed, their cart's top speed
// (its item's spec) with what items do to both now, and the fastest the
// player, their aim or their cart turns (at its top speed, full lock).
struct movement_limits {
    float walk_speed = 0.0f;  // world units a second
    float cart_speed = 0.0f;
    float turn_rate = 0.0f;   // rad/s
};

// The limits for a player in `cart` with `effects` running at `now_ms`
// (walking speed and the cart's top speed, game/effects.h).
movement_limits movement_limits_for(const game_tuning& tuning, const cart_spec& cart,
                                    const std::vector<active_effect>& effects, std::int64_t now_ms);

// The fastest the game moves a player in `mode`: walking speed on foot,
// their cart's top speed in a cart.
float mode_speed(std::uint8_t mode, const movement_limits& limits);

struct motion_check {
    bool allowed = false;
    // The motion slack left after this move: server.motion_distance_slack at
    // most, refilling at that many world units per second. A move may go
    // that much further than its speed allows, so the slack covers lag
    // without adding up over many quick calls.
    float slack_left = 0.0f;
    // How far the move counts for movement XP, in previous.mode (the mode the
    // stretch was covered in): the path `previous` traced when the move
    // ended where it leads, else the straight line; never more than that
    // mode's own speed over the time, as offline.
    float earned_distance = 0.0f;  // world units
};

// Moving from `previous.position` to `to` (XZ) in `seconds` (at most
// server.max_motion_gap_seconds count), having moved as `previous` (the last
// accepted motion): the faster of the two modes' speeds times
// server.motion_speed_scale, plus the slack left.
motion_check check_motion(const net_motion& previous,
                          const glm::vec3& to,
                          float seconds,
                          std::uint8_t mode,
                          float slack_left,
                          const movement_limits& limits,
                          const game_tuning& tuning);

// What others extrapolate an avatar with is limited to what the game can
// produce: speed to `mode`'s (times server.motion_speed_scale), the turn
// rate to the fastest the player, aim or cart turns.
float clamp_motion_speed(std::uint8_t mode, float speed, const movement_limits& limits, const server_tuning& tuning);
float clamp_turn_rate(float turn_rate, const movement_limits& limits);

// Which movement XP a move earns, as offline: walking on foot, the cart
// rate only at a cart's wheel on a road (riding along earns nothing).
struct movement_earnings {
    bool walking = false;
    bool cart = false;
};
movement_earnings movement_earns(std::uint8_t mode, bool on_road);

// Within an interaction `radius` of `target` (XZ), plus server.interact_slack.
bool within_interact_reach(const glm::vec3& position, const glm::vec3& target, float radius, const server_tuning& tuning);

// --- Carts ---------------------------------------------------------------------

// Close enough to `seat` of `cart` to step into it: cart.board_reach, plus
// server.interact_slack.
bool within_seat_reach(const cart_pose& cart, cart_seat seat, const glm::vec3& position, const game_tuning& tuning);

// Whether a driver may say their cart went over to `to` on `ground` (where
// the server has it, facing `yaw`), last moving at `speed` (m/s along its
// heading) and turning at `turn_rate`, in real cart time (the game's over
// cart.time_scale): onto a side when that turn and the
// side slope together come within server.cart_condition_slack of lifting
// its wheels (physics/cart_dynamics.h), sunk when the water there comes
// within that share of its wade depth.
bool overturn_plausible(cart_condition to, const cart_ground& ground, float yaw, float speed, float turn_rate,
                        const cart_spec& cart, const server_tuning& tuning);

// The first parking slot beside hole 1 (parked_cart_pose) with no cart of
// the room parked within half a park_spacing of it.
std::size_t free_parking_slot(const std::vector<glm::vec3>& carts, const glm::vec3& start, const glm::vec3& pin,
                              const cart_tuning& tuning);

// --- Items ---------------------------------------------------------------------

// Using `item_id` at `now_ms` (UTC milliseconds): an item with a use, the one
// equipped in its slot of `equipped`, its last use (from `last_used_ms`, 0:
// never) over by server.timing_slack_seconds before the server's clock says
// so. The error id, or empty when it may be used.
std::string check_item_use(const std::string& item_id, const loadout& equipped, std::int64_t last_used_ms,
                           std::int64_t now_ms, const server_content& content);

// --- Shots ---------------------------------------------------------------------

// What a client asks for: its setup's dials and club (game/shot_setup.h),
// played from the server's own ball in the hand the server has for them,
// and which greens' flags it has out (each a hole index).
struct shot_request {
    int stroke = 0;  // the stroke this shot is, from 1
    float aim_angle = 0.0f;
    float face_open = 0.0f;
    float ball_position = 0.0f;
    float backswing = 0.0f;
    std::string club_id;
    std::int64_t shot_at = 0;  // UTC milliseconds, by the client's estimate of my clock
    std::vector<int> flags_out;
};

// The server's ball on the hole being played.
struct server_ball {
    glm::vec3 position{0.0f};
    int stroke_count = 0;
    std::int64_t last_shot_at = 0;  // the last shot's shot_at; 0 before the first
    ball_lie lie = ball_lie::fairway;  // what it lies on: the stock setup's face from sand
};

// The player as the server has them when they shoot: who they are as a
// golfer, the club in their hand, whether their bag is on their back, and
// which greens' flags are out in their room.
struct server_golfer {
    golfer_profile golfer;
    std::string club_in_hand;
    bool bag_on_back = false;
    std::vector<int> flags_out;
};

struct shot_check {
    std::optional<shot_setup> input;  // what to simulate, when the shot is valid
    std::string error;
};

// The next stroke, hit from beside the ball (player.ball_interact_radius,
// plus slack), with the club in hand and the bag set down, the flags as the
// room has them, an aim wrapped into [-pi, pi] (wrap_angle), every dial
// finite and within the golfer's limits (shot_ability.h), and a shot time
// within server.shot_time_slack_seconds of `now_unix_ms` and no earlier than
// the last shot's, so a player cannot pick a calm moment.
shot_check check_shot(const shot_request& request,
                      const server_ball& ball,
                      const glm::vec3& player_position,
                      const server_golfer& golfer,
                      std::int64_t now_unix_ms,
                      const server_content& content);

// A shot's dice, which the server picks when it plays it: from its clock,
// the account and the stroke, so no client knows them before.
std::uint64_t server_shot_seed(std::int64_t timestamp_micros, std::uint64_t account_id, int stroke);

// --- The bag -------------------------------------------------------------------

// A player at `player` moving their bag from `from` to `to` (game/golf_bag.h
// move_bag): onto their back from the ground or their cart within
// player.bag_reach (plus slack) of where it is; from their back onto their
// cart within reach of that seat's spot, or down within reach of where they
// stand. The error id, or empty.
std::string check_bag_move(const bag_place& from, const bag_place& to, const glm::vec3& player, const cart_pose& cart,
                           const game_tuning& tuning);

// Taking `club_id` from the bag at `bag`: a club in the bag, and the bag set
// down or on the cart within reach of the player at `player`.
std::string check_take_club(const std::string& club_id, const bag_place& bag, const glm::vec3& player, const cart_pose& cart,
                            const server_content& content);
