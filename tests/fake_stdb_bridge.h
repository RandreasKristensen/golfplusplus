#pragma once

// A stand-in for the Rust bridge's C API (src/net/stdb_bridge.h), so tests
// drive net_client without Rust or a server: it records every call and
// hands out the events a test queues. The C API is free functions, so the
// fake's state is one shared object; reset() it at the start of each test.

#include "net/online_config.h"
#include "net/stdb_bridge.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

struct fake_stdb_bridge {
    struct call {
        std::string name;    // the C function, e.g. "stdb_take_shot"
        std::string text;    // its text argument, if any
        std::int64_t number = 0;
        stdb_motion motion{};
        stdb_shot shot{};
        std::string club_id;  // shot.club_id, copied
    };

    std::vector<call> calls;
    std::vector<std::vector<std::string>> subscriptions;  // queries, by handle - 1
    std::vector<std::uint32_t> unsubscribed;
    std::deque<stdb_event> events;
    std::deque<std::string> texts;  // what fake_text's strings point into, until reset()
    bool connected = false;
    bool wrong_layout = false;  // stdb_layout disagrees with the header
    bool busy = false;          // stdb_begin_login refuses: signing in or connected already

    void reset();
    std::vector<std::string> call_names() const;
};

fake_stdb_bridge& fake_bridge();

// An anonymous login to a local server.
online_config fake_online_config();

// An event of `kind` with nothing else set.
stdb_event fake_event(stdb_event_kind kind);

// Queues an event for the next stdb_poll. Strings must outlive the poll.
void queue_event(const stdb_event& event);
stdb_event row_event(stdb_table table, stdb_row_change change, const stdb_row& row);
// A copy of `text` kept by the bridge, so it outlives the poll even when
// `text` is a temporary.
stdb_string fake_text(const std::string& text);
