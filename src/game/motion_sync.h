#pragma once

// When the local player's movement goes to the server in online play, and
// what it carries. Online movement is by input: every fixed step I take on
// foot or at my cart's wheel (game/movement_step.h) is numbered and sent as
// runs of inputs, which the server steps from its own state; the motion
// beside them is where that leaves me, for my prediction and for others.
// Motion is sparse: others carry it on with extrapolate_motion
// (game/net_types.h), so it is sent when the intent changes (mode, starting,
// stopping or reversing a move or a turn, a cart getting on or off a road,
// any dial of my setup turned while I address my ball, crouching to read the
// green or standing up),
// as a heartbeat while moving or holding the wheel, as a correction when that
// extrapolation drifts too far from where the player really is or which way
// they face (the mouse turns without a turn rate), and before any action, so
// the server has played my steps first. Never more often than
// net.motion_min_interval_seconds otherwise.

#include "game/game_tuning.h"
#include "game/net_types.h"
#include "game/golf_cart.h"
#include "game/movement_step.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct game_state;

// Slower than these counts as not moving or turning: no movement XP.
// A parked driver still sends a heartbeat.
inline constexpr float still_speed = 0.05f;      // world units/s
inline constexpr float still_turn_rate = 0.01f;  // rad/s

struct predicted_motion {
    net_motion motion;
    double sent_at = 0.0;
    std::uint64_t step = 0;  // my last fixed step when it was sent
    cart_state cart;
    // On foot: how far I was put since without a step (onto a tee, out of
    // a seat), which the server's row may already show beside this motion.
    glm::vec3 placed{0.0f};
};

// Inputs retained until their motion is acknowledged, for fixed-step replay.
struct predicted_cart_step {
    std::uint64_t number = 0;
    cart_controls controls;
    cart_spec spec;
    float grip = 0.0f;
    float wetness = 0.0f;
};

struct sync_breach {
    std::string thing;
    glm::vec3 predicted{0.0f};
    glm::vec3 authoritative{0.0f};
    double ack = 0.0;
    std::string refusal;
};

struct motion_sync_state {
    std::optional<net_motion> last_sent;
    float since_sent = 0.0f;  // seconds
    std::optional<net_motion> queued;  // scheduling only; never an acknowledgement or sent history
    float since_queued = 0.0f;
    double clock = 0.0;
    double acknowledged = 0.0;
    double cart_acknowledged = 0.0;
    std::optional<room_cart> observed_cart;
    float seat_wait = 0.0f;
    // My last fixed step's number; it never goes back while the game runs
    // (reset_motion_sync keeps it), so the server never takes new steps for
    // ones it has played.
    std::uint64_t step = 0;
    // The steps taken since the last motion went out: the first's number,
    // where it started on foot, and the runs.
    std::vector<move_run> unsent;
    std::uint64_t unsent_first = 0;
    glm::vec3 unsent_start{0.0f};
    // On foot: my motion as my last step left it.
    std::optional<net_motion> stepped;
    std::deque<predicted_motion> history;
    std::deque<predicted_cart_step> cart_steps;
    std::string last_refusal;
    std::uint64_t breaches = 0;
    std::deque<sync_breach> reports;
    float ride_since = 0.0f;  // since my last ride motion (game/ride_sync.h)
};

bool should_send_motion(const motion_sync_state& sync, const net_motion& now, const net_tuning& tuning);

// The local player's motion this frame.
net_motion current_motion(const game_state& state);

// Online: pushes a motion command when should_send_motion says so, except
// while a shot plays or the ball blends to the server's rest (the server may
// have finished the hole already). Offline: nothing.
void sync_motion(game_state& state, float dt);

// Online: pushes the current motion now, whatever the interval, so the
// server knows where the player stands before an action it checks the
// reach of (entering a hole, claiming a collectible). Offline: nothing.
void send_motion_now(game_state& state);

// Online: numbers one fixed step about to be taken and keeps its input for
// the next motion. A step on foot from elsewhere than my last one ended (I
// was put there without a step) or of another kind sends what came before
// first. Offline: nothing but the number.
std::uint64_t record_step(game_state& state, const move_input& input);
// After a step on foot: where it left me, as the server will have it.
void stepped_on_foot(game_state& state);
// Online: sends the steps not yet sent, before an action the server judges
// by where I stand or that moves me, so it plays them first.
void send_steps(game_state& state);
// Forgets all pending motion and prediction, keeping the step number.
void reset_motion_sync(game_state& state);

// Called only after the bridge accepts a motion for transmission.
void motion_sent(game_state& state, const net_command& command, double id);
// Compares echoed ids with their predictions; bounds every unacknowledged prediction.
void reconcile_motion(game_state& state, float dt);
// A refused motion resets the affected prediction to its complete row.
void refuse_motion(game_state& state);
