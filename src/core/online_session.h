#pragma once

// app's side of online play, between the menus and net_client: tells the
// menus how the connection is (online_menu_status), carries out what they
// ask (online_request), and keeps an online round in its room. After a
// silent reconnect the server has ended the room state, so the session joins
// the course again and app restarts the round where the server puts the
// player: hole 1's start, a fresh round. Owned by app; built only with
// online play (tests drive it through the fake bridge).

#include "core/startup_flow.h"
#include "game/game_state.h"
#include "game/net_types.h"
#include "net/net_client.h"

#include <chrono>
#include <string>
#include <vector>

// What an online round has to do this frame.
enum class online_round_event {
    none,
    restart,  // back in the room after reconnecting: start the course over
    lost      // the connection or the room is gone: back to the menus
};

struct online_round_status {
    online_round_event event = online_round_event::none;
    // lost: a string table key saying why, or empty when the sign-in screen
    // says it (the connection failed).
    std::string message_key;
};

class online_session {
public:
    explicit online_session(net_client client);

    // net_client::update, then this frame's refused reducers.
    void update(game_state& state);

    // What the menus read; `now` times the link code's expiry.
    online_menu_status menu_status(const game_state& state, std::chrono::system_clock::time_point now) const;
    void carry_out(const std::vector<online_request>& requests, game_state& state);

    // Called every frame an online round plays.
    online_round_status watch_round(const game_state& state);
    // The player left the round for the menus.
    void leave_round();

    // The reducers the server refused this frame.
    const std::vector<reducer_failure>& refusals() const { return refusals_; }

private:
    net_client net_;
    std::vector<reducer_failure> refusals_;
    bool rejoining_ = false;    // the connection dropped during the round
    bool rejoin_sent_ = false;  // join_course went out for this reconnect
};
