#include "doctest.h"

#include "core/online_session.h"
#include "fake_stdb_bridge.h"
#include "game/net_types.h"
#include "game/text_ids.h"
#include "net/net_client.h"

#include "test_support.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr std::uint64_t my_account = 7;

// Strings in queued rows must outlive the poll that hands them out.
const std::string anna = "ANNA";
const std::string first_course = "first";

stdb_event account_row(const std::uint64_t account_id, const std::string& name) {
    stdb_row row{};
    row.player.account_id = account_id;
    row.player.display_name = fake_text(name);
    return row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, row);
}

stdb_event member_row(const std::uint64_t room_id, const stdb_row_change change) {
    stdb_row row{};
    row.room_member.account_id = my_account;
    row.room_member.room_id = room_id;
    return row_event(STDB_TABLE_ROOM_MEMBER, change, row);
}

stdb_event room_row(const std::uint64_t room_id, const int players) {
    stdb_row row{};
    row.room = stdb_room{room_id, fake_text(first_course), players};
    return row_event(STDB_TABLE_ROOM, STDB_ROW_INSERT, row);
}

void connect(online_session& session, game_state& state) {
    fake_bridge().connected = true;
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(account_row(my_account, anna));
    session.update(state);
}

void join_room(online_session& session, game_state& state, const std::uint64_t room_id) {
    queue_event(member_row(room_id, STDB_ROW_INSERT));
    queue_event(room_row(room_id, 2));
    session.update(state);
}

// Signed in as ANNA, in room 3 playing course "first".
online_session playing_session(game_state& state) {
    fake_bridge().reset();
    online_session session(*net_client::create(fake_online_config(), "signed in", "failed"));
    session.carry_out({online_request{online_request_type::begin_silent_login, {}}}, state);
    connect(session, state);
    join_room(session, state, 3);
    state.play = play_mode::online;
    state.course.id = first_course;
    return session;
}

bool called(const std::string& name, const std::string& text = {}) {
    for (const fake_stdb_bridge::call& call : fake_bridge().calls) {
        if (call.name == name && call.text == text) {
            return true;
        }
    }
    return false;
}
}

TEST_CASE("the session tells the menus who is signed in, their room and how linking goes") {
    game_state state = started_hole();
    online_session session = playing_session(state);
    REQUIRE(!fake_bridge().calls.empty());
    CHECK(called("stdb_begin_login"));
    CHECK(fake_bridge().calls[1].number == 1);  // silent

    const auto now = std::chrono::system_clock::now();
    online_menu_status status = session.menu_status(state, now);
    CHECK(status.available);
    CHECK(status.status == net_status::connected);
    CHECK(status.account_id == my_account);
    CHECK(status.name == "ANNA");
    CHECK(status.room_course_id == "first");
    REQUIRE(state.online.room.has_value());
    CHECK(state.online.room->room_id == 3U);
    CHECK(state.online.room->player_count == 2);

    const std::string code = "ABCD2345";
    const std::int64_t now_micros = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    stdb_row row{};
    row.link_code = stdb_link_code{fake_text(code), now_micros + 90 * 1000000LL};
    queue_event(row_event(STDB_TABLE_MY_LINK_CODE, STDB_ROW_INSERT, row));
    const std::string result = "invalid_code";
    stdb_row link_status{};
    link_status.link_status = stdb_link_status{fake_text(result), 0};
    queue_event(row_event(STDB_TABLE_MY_LINK_STATUS, STDB_ROW_INSERT, link_status));
    stdb_event refused = fake_event(STDB_EVENT_REDUCER_FAILED);
    const std::string reducer = "claim_name";
    const std::string error = "name_taken";
    refused.reducer = fake_text(reducer);
    refused.text = fake_text(error);
    queue_event(refused);
    session.update(state);

    status = session.menu_status(state, now);
    CHECK(status.link_code == "ABCD2345");
    CHECK(status.link_code_minutes_left == 2);
    CHECK(status.link_result == "invalid_code");
    REQUIRE(status.reducer_failures.size() == 1U);
    CHECK(status.reducer_failures[0].error == "name_taken");
    // Refusals are this frame's only; an expired code is no code.
    session.update(state);
    status = session.menu_status(state, now + std::chrono::minutes(2));
    CHECK(status.reducer_failures.empty());
    CHECK(status.link_code.empty());

    // Leaving the room forgets it.
    queue_event(member_row(3, STDB_ROW_DELETE));
    session.update(state);
    CHECK(!state.online.room.has_value());
    CHECK(session.menu_status(state, now).room_course_id.empty());
}

TEST_CASE("the menus' requests reach the server") {
    game_state state = started_hole();
    online_session session = playing_session(state);
    session.carry_out({online_request{online_request_type::claim_name, "BOB"},
                       online_request{online_request_type::redeem_link_code, "ABCD2345"},
                       online_request{online_request_type::join_course, "first"},
                       online_request{online_request_type::create_link_code, {}}},
                      state);
    CHECK(called("stdb_claim_name", "BOB"));
    CHECK(called("stdb_redeem_link_code", "ABCD2345"));
    CHECK(called("stdb_join_course", "first"));
    CHECK(called("stdb_create_link_code"));

    session.carry_out({online_request{online_request_type::sign_out, {}}}, state);
    CHECK(called("stdb_sign_out"));
    CHECK(state.online.account_id == 0U);
    CHECK(session.menu_status(state, std::chrono::system_clock::now()).status == net_status::signed_out);
}

TEST_CASE("an online round stays put while its room is there") {
    game_state state = started_hole();
    online_session session = playing_session(state);
    CHECK(session.watch_round(state).event == online_round_event::none);
    session.leave_round();
    CHECK(called("stdb_leave_room"));
}

TEST_CASE("after a silent reconnect an online round joins its course again and restarts") {
    game_state state = started_hole();
    online_session session = playing_session(state);

    stdb_event lost = fake_event(STDB_EVENT_DISCONNECTED);
    lost.retrying = true;
    queue_event(lost);
    session.update(state);
    CHECK(session.watch_round(state).event == online_round_event::none);
    CHECK(state.online.status == net_status::signing_in);
    CHECK(!called("stdb_join_course", "first"));

    connect(session, state);
    CHECK(session.watch_round(state).event == online_round_event::none);
    CHECK(called("stdb_join_course", "first"));

    join_room(session, state, 4);
    CHECK(session.watch_round(state).event == online_round_event::restart);
    CHECK(session.watch_round(state).event == online_round_event::none);
}

TEST_CASE("an online round ends when the room goes or the connection fails") {
    game_state state = started_hole();
    online_session session = playing_session(state);
    queue_event(member_row(3, STDB_ROW_DELETE));
    session.update(state);
    online_round_status round = session.watch_round(state);
    CHECK(round.event == online_round_event::lost);
    CHECK(round.message_key == text_online_room_lost);

    session = playing_session(state);
    stdb_event failed = fake_event(STDB_EVENT_DISCONNECTED);
    const std::string reason = "connection_lost: gone";
    failed.text = fake_text(reason);
    queue_event(failed);
    session.update(state);
    round = session.watch_round(state);
    CHECK(round.event == online_round_event::lost);
    CHECK(round.message_key.empty());
    CHECK(session.menu_status(state, std::chrono::system_clock::now()).failure_id == "connection_lost");
}
