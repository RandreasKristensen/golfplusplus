#pragma once

// The game's connection to its SpacetimeDB server, through the Rust bridge
// (net/stdb_bridge.h). app owns one and calls update every frame. Game code
// never sees it: it pushes game_state::net_commands, which update sends, and
// reads game_state::online, which update fills from the server's rows.
//
// Subscriptions follow the plan's traffic rules: the caller's own views on
// connecting, then the caller's own rows once the account is known, then the
// room's rows while in a room (net/room_rows.h). Nothing subscribes to a
// whole table.
//
// Refused gameplay reducers (what net_commands send) go to the game in
// game_state::online; the menus' own go to take_reducer_failure.

#include "game/game_state.h"
#include "game/save_data.h"
#include "net/online_config.h"
#include "net/room_rows.h"
#include "net/stdb_bridge.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Every struct's size and field offset as stdb_bridge.h lays them out, in
// the order of ffi::layout in net/client_bridge/src/ffi.rs: what the
// bridge's stdb_layout must report for the two to work together.
std::vector<std::size_t> stdb_header_layout();

// The id part of a failure ("id: detail").
std::string failure_id(const std::string& failure);

// My account's link code (the my_link_code view).
struct link_code_info {
    std::string code;
    std::int64_t expires_at_micros = 0;  // server clock, since the Unix epoch
};

class net_client {
public:
    // nullopt when the bridge was built for another layout of its C API.
    // The pages are what the browser shows after signing in, or failing to.
    static std::optional<net_client> create(const online_config& config,
                                            const std::string& page_signed_in,
                                            const std::string& page_failed);

    // Signs in with the stored sign-in, else through the browser.
    void begin_login();
    // Signs in only if that needs no browser: a failure with
    // net_failure_no_stored_sign_in means the player has to sign in.
    void begin_silent_login();
    void cancel_login();
    // Disconnects, forgets the stored sign-in and clears `state.online`.
    void sign_out(game_state& state);

    // What the menus ask of the server; gameplay goes through net_commands.
    // A refusal arrives as a reducer failure.
    void claim_name(const std::string& name);
    void join_course(const std::string& course_id);
    void leave_room();
    void create_group();
    void join_group(std::uint64_t group_id);
    void leave_group();
    void return_to_hub();
    void create_link_code();
    void redeem_link_code(const std::string& code);

    // Sends `state`'s net_commands, advances the connection and applies what
    // the server sent to `state.online`.
    void update(game_state& state);

    net_status status() const { return status_; }
    // Signed in anonymously: a guest, whose account the server deletes when
    // it disconnects, and who cannot link logins.
    bool guest() const { return guest_; }
    // Why sign-in or the connection failed ("id: detail", ids in
    // stdb_bridge.h), or the last subscription failure.
    const std::string& failure() const { return failure_; }
    // The oldest refused menu reducer not taken yet.
    std::optional<reducer_failure> take_reducer_failure();

    const std::optional<link_code_info>& link_code() const { return link_code_; }
    // How the last redeem_link_code went (link_* in account_rules.h), and
    // how many outcomes have arrived: a new one changes the count.
    const std::string& link_result() const { return link_result_; }
    std::uint64_t link_results() const { return link_results_; }

private:
    struct client_deleter {
        void operator()(stdb_client* client) const;
    };

    struct collected_state {
        std::string collectible_id;
        bool repeatable = false;
        int claim_count = 0;
        int claimed_at_holes_completed = -1;
    };

    explicit net_client(stdb_client* client);

    void start_login(bool silent_only);
    // Microseconds on this client's own clock.
    std::int64_t local_now() const;
    void send_commands(game_state& state);
    void handle(const stdb_event& event, game_state& state);
    void handle_row(const stdb_event& event, game_state& state);
    // Gives the game the progress rows changed so far, once complete.
    void deliver_progress(game_state& state);
    void subscribe_self();
    void subscribe_room(std::uint64_t room_id);
    // A new connection or a sign-out: its subscriptions are gone.
    void forget_session(game_state& state);
    // Drops my rows and their subscriptions, and clears `state.online`.
    void forget_progress(game_state& state);
    save_data progress() const;

    std::unique_ptr<stdb_client, client_deleter> client_;
    std::chrono::steady_clock::time_point started_ = std::chrono::steady_clock::now();
    net_status status_ = net_status::signed_out;
    bool guest_ = false;
    std::string failure_;
    std::deque<reducer_failure> reducer_failures_;  // oldest first
    std::optional<link_code_info> link_code_;
    std::string link_result_;
    std::uint64_t link_results_ = 0;

    std::uint64_t account_id_ = 0;  // 0 until my_account arrives
    std::uint64_t room_id_ = 0;     // 0 outside a room
    std::uint32_t self_subscription_ = 0;
    std::uint32_t room_subscription_ = 0;
    room_rows room_;
    server_clock clock_;
    bool self_applied_ = false;     // the first progress snapshot is complete
    bool progress_changed_ = false;

    // My progress rows, keyed by row id.
    int holes_completed_ = 0;
    std::map<std::uint64_t, std::pair<std::string, int>> skills_;
    std::map<std::uint64_t, std::string> completed_courses_;
    std::map<std::uint64_t, collected_state> collected_;
    std::map<std::uint64_t, std::string> world_flags_;
};
