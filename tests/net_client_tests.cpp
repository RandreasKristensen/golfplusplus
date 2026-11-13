#include "doctest.h"

#include "fake_stdb_bridge.h"
#include "game/mode_dispatch.h"
#include "game/hub_ball.h"
#include "game/server_errors.h"
#include "game/online_play.h"
#include "game/net_types.h"
#include "game/progression.h"
#include "game/remote_players.h"
#include "game/scorecard.h"
#include "net/net_client.h"
#include "net/online_config.h"

#include "test_support.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {
// A client signed in and connected, with my_account received.
net_client connected_client(game_state& state, const std::uint64_t account_id) {
    fake_bridge().reset();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    fake_bridge().connected = true;
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    stdb_row row{};
    row.player.account_id = account_id;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, row));
    client.update(state);
    return client;
}

// A my_skills row of golfer `golfer_id`.
stdb_event skill_row(const std::uint64_t id, const std::uint64_t golfer_id, const std::string& skill, const std::int64_t xp,
                     const stdb_row_change change = STDB_ROW_INSERT) {
    stdb_row row{};
    row.golfer_skill = stdb_golfer_skill{id, golfer_id, fake_text(skill), xp};
    return row_event(STDB_TABLE_MY_SKILLS, change, row);
}

stdb_event member_row(const std::uint64_t account_id, const std::uint64_t room_id, const stdb_row_change change) {
    stdb_row row{};
    row.room_member.account_id = account_id;
    row.room_member.room_id = room_id;
    return row_event(STDB_TABLE_ROOM_MEMBER, change, row);
}

stdb_event applied(const std::uint32_t subscription) {
    stdb_event event = fake_event(STDB_EVENT_SUBSCRIPTION_APPLIED);
    event.subscription = subscription;
    return event;
}

bool any_query_contains(const std::vector<std::string>& queries, const std::string& text) {
    return std::any_of(queries.begin(), queries.end(), [&text](const std::string& q) { return q.find(text) != std::string::npos; });
}
}

TEST_CASE("the client refuses a bridge built for another layout") {
    fake_bridge().reset();
    fake_bridge().wrong_layout = true;
    CHECK(!net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash).has_value());
    fake_bridge().reset();
    CHECK(net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash).has_value());
    REQUIRE(!fake_bridge().calls.empty());
    CHECK(fake_bridge().calls[0].text == "http://localhost:3000");
    CHECK(fake_bridge().calls[0].number == 1);  // anonymous
}

TEST_CASE("signing in moves through the statuses, and failures say why") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    CHECK(client.status() == net_status::signed_out);

    client.begin_login();
    CHECK(client.status() == net_status::signing_in);
    queue_event(fake_event(STDB_EVENT_LOGIN_WAITING_FOR_BROWSER));
    client.update(state);
    CHECK(client.status() == net_status::waiting_for_browser);

    const std::string reason = "cancelled";
    stdb_event failed = fake_event(STDB_EVENT_LOGIN_FAILED);
    failed.text = fake_text(reason);
    queue_event(failed);
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(client.failure() == "cancelled");

    client.begin_login();
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    client.update(state);
    CHECK(client.status() == net_status::connecting);
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    client.update(state);
    CHECK(client.status() == net_status::connecting);  // until the server's protocol arrives
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    client.update(state);
    CHECK(client.status() == net_status::connected);
    CHECK(client.failure().empty());
    CHECK(!client.guest());
}

TEST_CASE("an anonymous sign-in is a guest") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    stdb_event signed_in = fake_event(STDB_EVENT_SIGNED_IN);
    signed_in.login_method = STDB_LOGIN_ANONYMOUS;
    queue_event(signed_in);
    client.update(state);
    CHECK(client.guest());
}

TEST_CASE("subscriptions go from protocol and content to my views to my rows to my room, never a whole table") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    std::vector<std::vector<std::string>>& subscriptions = fake_bridge().subscriptions;
    REQUIRE(subscriptions.size() == 4U);
    CHECK(subscriptions[1] == std::vector<std::string>{"SELECT * FROM server_content_hash"});
    CHECK(subscriptions[0] == std::vector<std::string>{"SELECT * FROM server_protocol"});
    CHECK(any_query_contains(subscriptions[2], "my_account"));
    CHECK(any_query_contains(subscriptions[3], "FROM my_skills"));
    CHECK(any_query_contains(subscriptions[3], "FROM my_golfer_progress"));
    CHECK(any_query_contains(subscriptions[3], "FROM room_member WHERE account_id = 42"));
    // The score history stays on the server: nothing shows it.
    CHECK(!any_query_contains(subscriptions[3], "hole_score"));

    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    client.update(state);
    REQUIRE(subscriptions.size() == 5U);
    CHECK(any_query_contains(subscriptions[4], "FROM my_avatar_motion"));
    CHECK(any_query_contains(subscriptions[4], "FROM shot_event WHERE room_id = 7"));
    CHECK(any_query_contains(subscriptions[4], "FROM my_golf_carts"));
    CHECK(any_query_contains(subscriptions[4], "room_member.room_id = 7"));

    // Someone else's row changes nothing; my move to another room swaps the subscription.
    queue_event(member_row(43, 9, STDB_ROW_INSERT));
    client.update(state);
    CHECK(subscriptions.size() == 5U);
    queue_event(member_row(42, 8, STDB_ROW_UPDATE));
    client.update(state);
    REQUIRE(subscriptions.size() == 6U);
    CHECK(fake_bridge().unsubscribed == std::vector<std::uint32_t>{5});
    CHECK(any_query_contains(subscriptions[5], "WHERE room_id = 8"));

    queue_event(member_row(42, 8, STDB_ROW_DELETE));
    client.update(state);
    const std::vector<std::uint32_t> both{5, 6};
    CHECK(fake_bridge().unsubscribed == both);

    // Only the protocol and content views are global.
    for (std::size_t i = 2; i < subscriptions.size(); ++i) {
        for (const std::string& query : subscriptions[i]) {
            CHECK((query.find("WHERE") != std::string::npos || query.find("my_") != std::string::npos));
        }
    }
}

TEST_CASE("the game's net commands become reducer calls, once") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    fake_bridge().calls.clear();

    state.play = play_mode::online;
    update_game(state, game_input{}, 0.016f);  // the first motion update
    shot_setup shot;
    shot.club_id = "putter";
    shot.aim_angle = 1.5f;
    shot.backswing = 0.6f;
    shot.shot_at = 2000;
    net_command take;
    take.type = net_command_type::take_shot;
    take.shot = shot;
    take.stroke = 3;
    state.net_commands.push_back(take);
    net_command enter;
    enter.type = net_command_type::tee_up;
    enter.hole_index = 4;
    enter.give_up = true;
    state.net_commands.push_back(enter);
    net_command claim;
    claim.type = net_command_type::claim_collectible;
    claim.collectible_id = "lost_ball";
    state.net_commands.push_back(claim);
    net_command smoke;
    smoke.type = net_command_type::use_item;
    smoke.item_id = "cigarette";
    state.net_commands.push_back(smoke);

    client.update(state);
    const std::vector<fake_stdb_bridge::call>& calls = fake_bridge().calls;
    const std::vector<std::string> expected{"stdb_update_motion", "stdb_take_shot", "stdb_tee_up",
                                            "stdb_claim_collectible", "stdb_use_item"};
    REQUIRE(fake_bridge().call_names() == expected);
    CHECK(calls[0].motion.x == state.player.position.x);
    CHECK(calls[1].shot.stroke == 3);
    CHECK(calls[1].club_id == "putter");
    CHECK(calls[1].shot.backswing == 0.6f);
    CHECK(calls[1].shot.shot_at == 2000);
    CHECK(calls[2].number == 4);
    CHECK(calls[2].flag);
    CHECK(calls[3].text == "lost_ball");
    CHECK(calls[4].text == "cigarette");
    CHECK(state.net_commands.empty());

    client.update(state);
    CHECK(fake_bridge().calls.size() == 5U);
}

TEST_CASE("commands made while not connected are dropped, not sent later") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    net_command enter;
    enter.type = net_command_type::tee_up;
    state.net_commands.push_back(enter);
    client.update(state);
    CHECK(state.net_commands.empty());
    CHECK(fake_bridge().call_names() == std::vector<std::string>{"stdb_create"});
}

TEST_CASE("my progress rows become online progress, the first snapshot without drops") {
    game_state state = started_hole();
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    const std::uint32_t self = 4;

    queue_event(skill_row(1, 8, "fitness", 500));
    client.update(state);
    CHECK(!state.online.progress_received);  // the snapshot is not complete yet

    stdb_row flag{};
    flag.world_flag = stdb_world_flag{5, 8, fake_text("found_lost_ball")};
    queue_event(row_event(STDB_TABLE_MY_WORLD_FLAGS, STDB_ROW_INSERT, flag));
    stdb_row token{};
    token.collected = stdb_collected{6, 8, fake_text("range_token"), true, 2, 3};
    queue_event(row_event(STDB_TABLE_MY_COLLECTED, STDB_ROW_INSERT, token));
    stdb_row golfer{};
    golfer.golfer_progress = stdb_golfer_progress{8, 42, 0, 4};
    queue_event(row_event(STDB_TABLE_MY_GOLFER_PROGRESS, STDB_ROW_INSERT, golfer));
    queue_event(applied(self));
    client.update(state);

    REQUIRE(state.online.progress_received);
    const save_data& progress = state.online.progress;
    CHECK(skill_xp(progress.skills, "fitness") == 500);
    CHECK(progress.world_flags == std::vector<std::string>{"found_lost_ball"});
    CHECK(progress.holes_completed == 4);
    REQUIRE(progress.repeatable_collectibles.count("range_token") == 1U);
    CHECK(progress.repeatable_collectibles.at("range_token").claim_count == 2);
    CHECK(progress.repeatable_collectibles.at("range_token").claimed_at_holes_completed == 3);
    CHECK(state.xp_drops.empty());
    CHECK(&active_progress(state) == &state.online.progress);

    // A view's row changes as the new row and the old one deleted, in
    // either order: the old one leaving never takes the new one with it.
    const std::int64_t raised = 500 + state.tuning.xp_drops.min_visible_xp;
    queue_event(skill_row(1, 8, "fitness", raised));
    queue_event(skill_row(1, 8, "fitness", 500, STDB_ROW_DELETE));
    client.update(state);
    CHECK(skill_xp(state.online.progress.skills, "fitness") == raised);
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].skill_id == "fitness");

    queue_event(skill_row(1, 8, "fitness", raised, STDB_ROW_DELETE));
    queue_event(skill_row(1, 8, "fitness", 3000000000));
    client.update(state);
    CHECK(skill_xp(state.online.progress.skills, "fitness") == 3000000000);
}

TEST_CASE("a lost connection forgets the session's rows") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(skill_row(1, 42, "fitness", 500));
    queue_event(applied(4));
    client.update(state);
    REQUIRE(state.online.progress_received);

    const std::string reason = "connection_lost: token expired";
    stdb_event lost = fake_event(STDB_EVENT_DISCONNECTED);
    lost.text = fake_text(reason);
    lost.retrying = true;
    queue_event(lost);
    client.update(state);
    CHECK(client.status() == net_status::signing_in);
    CHECK(client.failure() == reason);
    CHECK(!state.online.progress_received);
    CHECK(state.online.progress.skills.empty());

    stdb_event gone = lost;
    gone.retrying = false;
    queue_event(gone);
    client.update(state);
    CHECK(client.status() == net_status::signed_out);
}

TEST_CASE("refused reducers queue up in order") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    const std::string reducer = "claim_name";
    const std::string taken = "name_taken";
    const std::string too_far = "too_far";
    stdb_event first = fake_event(STDB_EVENT_REDUCER_FAILED);
    first.reducer = fake_text(reducer);
    first.text = fake_text(taken);
    stdb_event second = first;
    second.text = fake_text(too_far);
    queue_event(first);
    queue_event(second);
    client.update(state);

    const std::optional<reducer_failure> oldest = client.take_reducer_failure();
    REQUIRE(oldest.has_value());
    CHECK(oldest->reducer == "claim_name");
    CHECK(oldest->error == "name_taken");
    CHECK(client.take_reducer_failure()->error == "too_far");
    CHECK(!client.take_reducer_failure().has_value());
}

TEST_CASE("a refused claim offers that collectible again, the oldest claim") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.online.pending_claims = {"lost_ball", "beer_can"};
    const std::string reducer = "claim_collectible";
    const std::string too_far = "too_far";
    stdb_event refused = fake_event(STDB_EVENT_REDUCER_FAILED);
    refused.reducer = fake_text(reducer);
    refused.text = fake_text(too_far);
    queue_event(refused);
    client.update(state);
    CHECK(state.online.pending_claims == std::vector<std::string>{"beer_can"});
}

TEST_CASE("a claim answered in the same update as a later refusal leaves both") {
    game_state state = started_hole();
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    const std::uint32_t self = 4;
    queue_event(applied(self));
    client.update(state);
    REQUIRE(state.online.progress_received);

    // Claim lost_ball (accepted), then beer_can (refused): both answers arrive together.
    state.online.pending_claims = {"lost_ball", "beer_can"};
    stdb_row found{};
    found.collected = stdb_collected{7, 8, fake_text("lost_ball"), false, 1, 0};
    queue_event(row_event(STDB_TABLE_MY_COLLECTED, STDB_ROW_INSERT, found));
    const std::string reducer = "claim_collectible";
    const std::string not_available = "not_available";
    stdb_event refused = fake_event(STDB_EVENT_REDUCER_FAILED);
    refused.reducer = fake_text(reducer);
    refused.text = fake_text(not_available);
    queue_event(refused);
    client.update(state);

    CHECK(state.online.pending_claims.empty());
    CHECK(state.online.progress.collected_ids == std::vector<std::string>{"lost_ball"});
}

TEST_CASE("menu actions call their reducers") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    fake_bridge().calls.clear();
    client.claim_name("Alpha");
    client.join_course("kalo_par_3", true);
    client.leave_room();
    client.create_link_code();
    client.redeem_link_code("ab2c-d3ef");
    const std::vector<std::string> expected{"stdb_claim_name", "stdb_join_course", "stdb_leave_room",
                                            "stdb_create_link_code", "stdb_redeem_link_code"};
    CHECK(fake_bridge().call_names() == expected);
    CHECK(fake_bridge().calls[0].text == "Alpha");
    CHECK(fake_bridge().calls[1].text == "kalo_par_3");
    CHECK(fake_bridge().calls[1].flag);
    CHECK(fake_bridge().calls[4].text == "ab2c-d3ef");
}

TEST_CASE("picking the ball out of its cup online is sent to the server") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.play = play_mode::online;
    fake_bridge().calls.clear();
    record_ball_picked_up(state);
    client.update(state);
    const std::vector<std::string> names = fake_bridge().call_names();
    CHECK(std::find(names.begin(), names.end(), "stdb_pick_up_ball") != names.end());
}

TEST_CASE("a server with another protocol asks for an update and reads nothing else") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version + 1));
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(failure_id(client.failure()) == net_failure_outdated_game);
    CHECK(fake_bridge().subscriptions.size() == 1U);  // only the protocol
    const std::vector<std::string> names = fake_bridge().call_names();
    CHECK(std::find(names.begin(), names.end(), "stdb_disconnect") != names.end());

    // The bridge's disconnect that follows keeps the failure.
    queue_event(fake_event(STDB_EVENT_DISCONNECTED));
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(failure_id(client.failure()) == net_failure_outdated_game);

    // Signing in again starts over.
    client.begin_login();
    CHECK(client.status() == net_status::signing_in);
    CHECK(client.failure().empty());
}

TEST_CASE("a server without the protocol view asks for an update") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    stdb_event failed = fake_event(STDB_EVENT_SUBSCRIPTION_FAILED);
    failed.subscription = 1;
    failed.text = fake_text("subscription_failed: no such table");
    queue_event(failed);
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(failure_id(client.failure()) == net_failure_outdated_game);
}

TEST_CASE("content hash: no account or gameplay is read or sent before content matches") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(applied(1));
    client.update(state);
    CHECK(client.status() == net_status::connecting);
    REQUIRE(fake_bridge().subscriptions.size() == 2U);
    CHECK(fake_bridge().subscriptions[1] == std::vector<std::string>{"SELECT * FROM server_content_hash"});

    const std::size_t before_commands = fake_bridge().calls.size();
    client.claim_name("PLAYER");
    client.join_course("test", true);
    client.leave_room();
    client.create_link_code();
    client.redeem_link_code("CODE");
    CHECK(fake_bridge().calls.size() == before_commands);
    net_command command;
    command.type = net_command_type::take_wheel;
    state.net_commands.push_back(command);
    client.update(state);
    CHECK(state.net_commands.empty());
    const std::vector<std::string> calls = fake_bridge().call_names();
    CHECK(std::find(calls.begin(), calls.end(), "stdb_take_wheel") == calls.end());

    queue_event(server_content_event(shipped_content().content_hash));
    queue_event(applied(2));
    client.update(state);
    CHECK(client.status() == net_status::connected);
    REQUIRE(fake_bridge().subscriptions.size() == 3U);
    CHECK(any_query_contains(fake_bridge().subscriptions[2], "my_account"));
}

TEST_CASE("content hash: different content disconnects and cannot be revived by queued rows") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash + 1));
    queue_event(server_content_event(shipped_content().content_hash));
    queue_event(server_protocol_event(protocol_version));
    queue_event(fake_event(STDB_EVENT_DISCONNECTED));
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(failure_id(client.failure()) == net_failure_content_mismatch);
    CHECK(fake_bridge().subscriptions.size() == 2U);
    CHECK(state.online.account_id == 0);
    const std::vector<std::string> calls = fake_bridge().call_names();
    CHECK(std::count(calls.begin(), calls.end(), "stdb_disconnect") == 1);

    client.begin_login();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    client.update(state);
    CHECK(client.status() == net_status::connected);
    CHECK(client.failure().empty());
}

TEST_CASE("content hash: missing or empty compatibility views fail closed") {
    for (const bool protocol : {true, false}) {
        for (const bool empty : {true, false}) {
            fake_bridge().reset();
            game_state state = started_hole();
            net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
            client.begin_login();
            queue_event(fake_event(STDB_EVENT_CONNECTED));
            if (!protocol) {
                queue_event(server_protocol_event(protocol_version));
            }
            stdb_event event = fake_event(empty ? STDB_EVENT_SUBSCRIPTION_APPLIED : STDB_EVENT_SUBSCRIPTION_FAILED);
            event.subscription = protocol ? 1 : 2;
            queue_event(event);
            client.update(state);
            CHECK(client.status() == net_status::failed);
            CHECK(failure_id(client.failure()) == (protocol ? net_failure_outdated_game : net_failure_content_mismatch));
            CHECK(fake_bridge().subscriptions.size() == (protocol ? 1U : 2U));
        }
    }
}

TEST_CASE("content hash: a view's rows following its applied subscription, as the SDK sends them, still connect") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash);
    client.begin_login();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    client.update(state);
    queue_event(applied(1));
    queue_event(server_protocol_event(protocol_version));
    client.update(state);
    CHECK(client.status() == net_status::connecting);
    queue_event(applied(2));
    queue_event(server_content_event(shipped_content().content_hash));
    client.update(state);
    CHECK(client.status() == net_status::connected);
    CHECK(client.failure().empty());
}

TEST_CASE("content hash: a reconnect checks content again before restoring account or room state") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    queue_event(applied(5));
    client.update(state);
    REQUIRE(state.online.room_ready);
    const std::size_t before = fake_bridge().subscriptions.size();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash + 1));
    client.update(state);
    CHECK(client.status() == net_status::failed);
    CHECK(failure_id(client.failure()) == net_failure_content_mismatch);
    CHECK(state.online.account_id == 0);
    CHECK(!state.online.room_ready);
    CHECK(fake_bridge().subscriptions.size() == before + 2);
}

TEST_CASE("signing in while busy keeps the status") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    fake_bridge().busy = true;
    client.begin_login();
    CHECK(client.status() == net_status::connected);
}

TEST_CASE("signing out, then in again, starts my progress over") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(skill_row(1, 42, "fitness", 500));
    queue_event(applied(4));
    client.update(state);
    REQUIRE(state.online.progress_received);

    client.sign_out(state);
    CHECK(client.status() == net_status::signed_out);
    CHECK(!state.online.progress_received);
    CHECK(state.online.progress.skills.empty());

    // The same account again subscribes to its rows again.
    const std::size_t before = fake_bridge().subscriptions.size();
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    stdb_row me{};
    me.player.account_id = 42;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, me));
    client.update(state);
    REQUIRE(fake_bridge().subscriptions.size() == before + 4);
    CHECK(any_query_contains(fake_bridge().subscriptions.back(), "account_id = 42"));
}

TEST_CASE("linking this login to another account never mixes their progress") {
    game_state state = started_hole();
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    queue_event(skill_row(1, 8, "fitness", 500));
    queue_event(applied(4));
    client.update(state);
    REQUIRE(skill_xp(state.online.progress.skills, "fitness") == 500);

    stdb_row other{};
    other.player.account_id = 77;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, other));
    client.update(state);
    CHECK(std::find(fake_bridge().unsubscribed.begin(), fake_bridge().unsubscribed.end(), 4U) != fake_bridge().unsubscribed.end());
    CHECK(!state.online.progress_received);

    queue_event(skill_row(1, 8, "fitness", 500, STDB_ROW_DELETE));  // the old account's rows leave
    queue_event(skill_row(9, 12, "golf_swing", 50));
    queue_event(applied(5));
    client.update(state);
    CHECK(skill_xp(state.online.progress.skills, "fitness") == 0);
    CHECK(skill_xp(state.online.progress.skills, "golf_swing") == 50);
    CHECK(state.xp_drops.empty());
}

TEST_CASE("my golfer comes from my own player row, which my room's subscription never inserts again") {
    game_state state = started_hole();
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    const std::uint8_t fade = static_cast<std::uint8_t>(natural_shape_index(natural_shape::fade));
    stdb_row row{};
    row.player = stdb_player{42, fake_text("ME"), true, static_cast<std::uint8_t>(handedness::left), fade, 3};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    row.player = stdb_player{43, fake_text("OTHER"), true, 0, natural_shape_unset, 0};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    client.update(state);
    REQUIRE(state.online.golfer.has_value());
    CHECK((*state.online.golfer == golfer_row{handedness::left, fade, 3}));

    // Into a room: my row is already held, so no insert of it follows.
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    queue_event(applied(5));
    client.update(state);
    REQUIRE(state.online.room_ready);
    CHECK(natural_shape_picked(state));
    CHECK(my_golfer(state).natural == natural_shape::fade);

    stdb_event lost = fake_event(STDB_EVENT_DISCONNECTED);
    lost.retrying = true;
    queue_event(lost);
    client.update(state);
    CHECK(!state.online.golfer.has_value());
}

TEST_CASE("my room's rows become its players, balls, carts, loadouts, groups, shots and item uses") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    client.update(state);

    const std::string me = "ME";
    const std::string other = "OTHER";
    const std::string club = "driver";
    const std::string cigarette = "cigarette";
    const std::string beer = "beer";
    const std::string red_cart = "red_cart";
    const std::string golf_bag = "golf_bag";
    const std::string mirror = "mirror";
    const std::string fuzzy_dice = "fuzzy_dice";
    const std::vector<std::int32_t> strokes{4, 0};
    stdb_row row{};
    row.room_member = stdb_room_member{43, 7, 9, strokes.data(), strokes.size(), false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.player = stdb_player{42, fake_text(me), true, 0};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.player = stdb_player{43, fake_text(other), true, 0};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.avatar_motion = stdb_avatar_motion{43, 7, stdb_motion{1, 1.0f, 2.0f, 3.0f, 0.5f, 1.2f, 0.0f, 0.0}, 2000000};
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.avatar_motion = stdb_avatar_motion{42, 7, stdb_motion{0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0}, 9000000000};
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.ball = stdb_ball{43, 7, 1, 4.0f, 5.0f, 6.0f, 2, 1500, true, false};
    queue_event(row_event(STDB_TABLE_BALL, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.golf_cart = stdb_golf_cart{43, 7, 10.0f, 1.0f, -3.0f, 0.75f, 42, static_cast<std::uint8_t>(cart_condition::on_left_side)};
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.golf_group = stdb_golf_group{9, 7, 1};
    queue_event(row_event(STDB_TABLE_GOLF_GROUP, STDB_ROW_INSERT, row));
    row = stdb_row{};
    const std::int32_t flags_out[] = {3};
    const stdb_snapshot_entry snapshot[] = {stdb_snapshot_entry{1, 4.0f, 0.0f, 5.0f, 0.5f, false},
                                            stdb_snapshot_entry{0, 1.0f, 2.0f, 3.5f, 0.0f, true}};
    const std::string putting = "shape.putt";
    const stdb_skill_level levels[] = {stdb_skill_level{fake_text(putting), 12}};
    row.shot_event = stdb_shot_event{7, 43, 1, stdb_shot{2, 0.25f, 0.1f, -0.4f, 0.8f, -0.3f, fake_text(club), 3500, flags_out, 1},
                                     1, 2, 0.5f, levels, 1, 99, snapshot, 2, 1.0f, 2.0f, 3.0f, 7.0f, 8.0f, 9.0f, false};
    queue_event(row_event(STDB_TABLE_SHOT_EVENT, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.item_use_event = stdb_item_use_event{7, 42, fake_text(cigarette)};
    queue_event(row_event(STDB_TABLE_ITEM_USE_EVENT, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.item_use_event = stdb_item_use_event{7, 43, fake_text(beer)};
    queue_event(row_event(STDB_TABLE_ITEM_USE_EVENT, STDB_ROW_INSERT, row));
    const stdb_loadout_mount mounts[] = {stdb_loadout_mount{fake_text(mirror), fake_text(fuzzy_dice)}};
    row = stdb_row{};
    row.player_loadout =
        stdb_player_loadout{43, fake_text(red_cart), mounts, 1, fake_text(cigarette), fake_text(beer), fake_text(golf_bag)};
    queue_event(row_event(STDB_TABLE_PLAYER_LOADOUT, STDB_ROW_INSERT, row));
    client.update(state);
    CHECK(state.online.players.empty());
    CHECK(state.online.carts.empty());
    queue_event(applied(5));
    client.update(state);

    REQUIRE(state.online.players.size() == 2U);
    const room_player& them = state.online.players.at(43);
    CHECK(them.name == "OTHER");
    CHECK(them.group_id == 9U);
    CHECK((them.round_strokes == std::vector<int>{4, 0}));
    REQUIRE(them.motion.has_value());
    CHECK(them.motion->mode == motion_mode::walk);
    CHECK(near(them.motion->position, glm::vec3(1.0f, 2.0f, 3.0f)));
    CHECK(them.motion_at == 2000000);
    CHECK(state.online.players.at(42).name == "ME");

    // My own motion row sets the server clock: no earlier than its stamp.
    CHECK(state.online.server_now >= 9000000000);

    REQUIRE(state.online.balls.count(43) == 1U);
    CHECK(state.online.balls.at(43).stroke_count == 2);
    CHECK(state.online.balls.at(43).hole == 1);
    CHECK(state.online.balls.at(43).in_cup);
    REQUIRE(state.online.carts.count(43) == 1U);
    CHECK(near(state.online.carts.at(43).position, glm::vec3(10.0f, 1.0f, -3.0f)));
    CHECK(near(state.online.carts.at(43).yaw, 0.75f));
    CHECK(state.online.carts.at(43).passenger == 42U);
    CHECK(state.online.carts.at(43).condition == cart_condition::on_left_side);
    CHECK(state.online.group_sizes.at(9) == 1);
    REQUIRE(state.online.shots.size() == 1U);
    const room_shot& shot = state.online.shots.front();
    CHECK(shot.account_id == 43U);
    CHECK(shot.stroke == 2);
    CHECK(shot.record.setup.club_id == "driver");
    CHECK(near(shot.record.setup.ball_start, glm::vec3(1.0f, 2.0f, 3.0f)));
    CHECK(shot.record.setup.face_open == 0.1f);
    CHECK(shot.record.setup.ball_position == -0.4f);
    CHECK(shot.record.setup.backswing == 0.8f);
    CHECK(shot.record.setup.strike_timing == -0.3f);
    CHECK(shot.record.setup.hand == handedness::left);
    CHECK(shot.record.seed == 99U);
    REQUIRE(shot.record.snapshot.size() == 2U);
    CHECK(shot.record.snapshot[0].kind == snapshot_kind::cart);
    CHECK(shot.record.snapshot[1].shooter);
    CHECK(shot.golfer.natural == natural_shape::fade);
    CHECK(shot.golfer.spread_scale == 0.5f);
    CHECK(skill_level(shot.golfer, "shape.putt") == 12);
    CHECK(skill_level(shot.golfer, "shape.flop") == golfer_top_level);
    CHECK((shot.flags_out == std::vector<int>{3}));
    CHECK(near(shot.rest, glm::vec3(7.0f, 8.0f, 9.0f)));
    // Others' item uses go to the game to play; mine played as I used it.
    REQUIRE(state.online.item_uses.size() == 1U);
    CHECK(state.online.item_uses.front().account_id == 43U);
    CHECK(state.online.item_uses.front().item_id == "beer");
    REQUIRE(state.online.loadouts.count(43) == 1U);
    const loadout& theirs = state.online.loadouts.at(43);
    CHECK(theirs.cart == "red_cart");
    CHECK(theirs.cart_mounts.at("mirror") == "fuzzy_dice");
    CHECK(theirs.smokable == "cigarette");
    CHECK(theirs.drinkable == "beer");

    // Leaving the room forgets all of it.
    queue_event(member_row(42, 7, STDB_ROW_DELETE));
    client.update(state);
    CHECK(state.online.players.empty());
    CHECK(state.online.balls.empty());
    CHECK(state.online.carts.empty());
    CHECK(state.online.loadouts.empty());
    CHECK(state.online.group_sizes.empty());
}

TEST_CASE("my active effect rows are the effects the server runs on me; others' are not mine") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    const std::string item = "lucky_charm";
    const std::string target = "xp.all";
    stdb_row row{};
    row.running_effect = stdb_running_effect{5, 42, fake_text(item), fake_text(target), 1, 0.5f, 1, 30.0f, 1000, 31000};
    queue_event(row_event(STDB_TABLE_RUNNING_EFFECT, STDB_ROW_INSERT, row));
    row.running_effect.id = 6;
    row.running_effect.account_id = 43;
    queue_event(row_event(STDB_TABLE_RUNNING_EFFECT, STDB_ROW_INSERT, row));
    client.update(state);
    REQUIRE(state.online.effects.size() == 1U);
    const active_effect& effect = state.online.effects.front();
    CHECK(effect.item == "lucky_charm");
    CHECK(effect.effect.target == "xp.all");
    CHECK(effect.effect.bucket == effect_bucket::increased);
    CHECK(effect.effect.duration == effect_duration::seconds);
    CHECK(effect.started_at_ms == 1000);
    CHECK(effect.ends_at_ms == 31000);

    row.running_effect.id = 5;
    row.running_effect.account_id = 42;
    queue_event(row_event(STDB_TABLE_RUNNING_EFFECT, STDB_ROW_DELETE, row));
    client.update(state);
    CHECK(state.online.effects.empty());
}

TEST_CASE("refused gameplay goes to the game, refused menu actions to the menus") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    const std::string take_shot = "take_shot";
    const std::string join_course = "join_course";
    const std::string too_far = "too_far";
    stdb_event refused = fake_event(STDB_EVENT_REDUCER_FAILED);
    refused.reducer = fake_text(take_shot);
    refused.text = fake_text(too_far);
    queue_event(refused);
    refused.reducer = fake_text(join_course);
    queue_event(refused);
    client.update(state);

    REQUIRE(state.online.refusals.size() == 1U);
    CHECK(state.online.refusals[0].reducer == "take_shot");
    const std::optional<reducer_failure> menu = client.take_reducer_failure();
    REQUIRE(menu.has_value());
    CHECK(menu->reducer == "join_course");
    CHECK(!client.take_reducer_failure().has_value());
}

TEST_CASE("stepping into and out of another's cart reaches its reducers, whose refusals go to the game") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    net_command board;
    board.type = net_command_type::board_cart;
    board.cart_owner = 43;
    state.net_commands.push_back(board);
    net_command leave;
    leave.type = net_command_type::leave_cart;
    state.net_commands.push_back(leave);
    fake_bridge().calls.clear();
    client.update(state);
    REQUIRE(fake_bridge().calls.size() == 2U);
    CHECK(fake_bridge().calls[0].name == "stdb_board_cart");
    CHECK(fake_bridge().calls[0].number == 43);
    CHECK(fake_bridge().calls[1].name == "stdb_leave_cart");

    // Righting someone's cart.
    net_command right;
    right.type = net_command_type::right_cart;
    right.cart_owner = 44;
    state.net_commands.push_back(right);
    fake_bridge().calls.clear();
    client.update(state);
    REQUIRE(fake_bridge().calls.size() == 1U);
    CHECK(fake_bridge().calls[0].name == "stdb_right_cart");
    CHECK(fake_bridge().calls[0].number == 44);

    const std::string board_cart = reducer_board_cart;
    const std::string seat_taken = "seat_taken";
    stdb_event refused = fake_event(STDB_EVENT_REDUCER_FAILED);
    refused.reducer = fake_text(board_cart);
    refused.text = fake_text(seat_taken);
    queue_event(refused);
    client.update(state);
    REQUIRE(state.online.refusals.size() == 1U);
    CHECK(state.online.refusals[0].reducer == reducer_board_cart);
}

TEST_CASE("booking, bus, start, group leave and give-up reach their reducers") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    for (const auto type : {net_command_type::open_tee_book, net_command_type::book_tee, net_command_type::cancel_tee,
        net_command_type::take_bus, net_command_type::return_to_hub, net_command_type::start_booked_round,
        net_command_type::leave_group, net_command_type::give_up_hole}) {
        net_command command;
        command.type = type;
        command.reservation_id = 9;
        state.net_commands.push_back(command);
    }
    fake_bridge().calls.clear();
    client.update(state);
    const auto names = fake_bridge().call_names();
    for (const auto name : {"stdb_open_tee_book", "stdb_book_tee", "stdb_cancel_tee", "stdb_take_bus", "stdb_return_to_hub",
        "stdb_start_booked_round", "stdb_leave_group", "stdb_give_up_hole"}) {
        CHECK(std::find(names.begin(), names.end(), name) != names.end());
    }
}

TEST_CASE("sync: room snapshots wait for apply and ignore the previous room's rows and completion") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    stdb_row row{};
    row.golf_cart = stdb_golf_cart{42, 7, 1.0f, 2.0f, 3.0f, 0.5f, 0, 0, 42, 5.0};
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, row));
    client.update(state);
    CHECK(!state.online.room_ready);
    CHECK(state.online.players.empty());
    CHECK(state.online.carts.empty());
    queue_event(applied(5));
    client.update(state);
    REQUIRE(state.online.room_ready);
    REQUIRE(state.online.carts.count(42) == 1);
    CHECK(state.online.carts.at(42).driver == 42);
    CHECK(state.online.carts.at(42).motion_id == 5.0);

    queue_event(member_row(42, 8, STDB_ROW_UPDATE));
    client.update(state);
    CHECK(!state.online.room_ready);
    CHECK(state.online.carts.empty());
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_UPDATE, row));
    queue_event(member_row(42, 7, STDB_ROW_DELETE));
    queue_event(applied(5));
    client.update(state);
    CHECK(!state.online.room_ready);
    CHECK(state.online.carts.empty());
    row.golf_cart.room_id = 8;
    row.golf_cart.x = 20.0f;
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, row));
    queue_event(applied(6));
    client.update(state);
    REQUIRE(state.online.room_ready);
    REQUIRE(state.online.carts.count(42) == 1);
    CHECK(state.online.carts.at(42).position.x == 20.0f);

    stdb_event lost = fake_event(STDB_EVENT_DISCONNECTED);
    lost.retrying = true;
    queue_event(lost);
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_UPDATE, row));
    client.update(state);
    CHECK(!state.online.room_ready);
    CHECK(state.online.carts.empty());
}

TEST_CASE("motion interest: view eviction removes presentation and preserves room membership") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.play = play_mode::online;
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    queue_event(member_row(43, 7, STDB_ROW_INSERT));
    stdb_row avatar{};
    avatar.avatar_motion = stdb_avatar_motion{43, 7, stdb_motion{}, 2000000};
    stdb_row cart{};
    cart.golf_cart = stdb_golf_cart{43, 7, 1.0f, 0.0f, 1.0f, 0.0f, 0, 0, 0, 0.0};
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, avatar));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, cart));
    queue_event(applied(5));
    client.update(state);
    update_remote_players(state, 0.016f);
    REQUIRE(state.remote_avatars.count(43) == 1U);
    REQUIRE(state.online.carts.count(43) == 1U);

    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_DELETE, avatar));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_DELETE, cart));
    client.update(state);
    update_remote_players(state, 0.016f);
    REQUIRE(state.online.players.count(43) == 1U);
    CHECK(!state.online.players.at(43).motion);
    CHECK(state.online.carts.count(43) == 0U);
    CHECK(state.remote_avatars.count(43) == 0U);
    CHECK(state.online.room_ready);

    // The view's delete/insert update arrives as one complete frame, and
    // returning to interest needs no replacement subscription.
    avatar.avatar_motion.motion.x = 2.0f;
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, avatar));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, cart));
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_DELETE, avatar));
    avatar.avatar_motion.motion.x = 3.0f;
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, avatar));
    client.update(state);
    update_remote_players(state, 0.016f);
    REQUIRE(state.online.players.at(43).motion);
    CHECK(state.online.players.at(43).motion->position.x == 3.0f);
    CHECK(state.remote_avatars.count(43) == 1U);
    CHECK(state.online.carts.count(43) == 1U);
    CHECK(fake_bridge().subscriptions.size() == 5U);
}

TEST_CASE("motion interest: inserts before old deletes preserve motion acknowledgements and complete cart rows") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    stdb_row avatar{};
    avatar.avatar_motion = stdb_avatar_motion{42, 7, stdb_motion{}, 2000000};
    stdb_row cart{};
    cart.golf_cart = stdb_golf_cart{42, 7, 1.0f, 0.0f, 1.0f, 0.0f, 0, 0, 0, 0.0};
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, avatar));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, cart));
    queue_event(applied(5));
    client.update(state);

    const stdb_row previous_avatar = avatar;
    const stdb_row previous_cart = cart;
    avatar.avatar_motion.motion.client_time = 1.0;
    cart.golf_cart.driver = 42;
    cart.golf_cart.passenger = 43;
    // The SDK invokes inserts before deletes for keyless views. The pose
    // and timestamp can stay the same while an acknowledgement or seat changes.
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_INSERT, avatar));
    queue_event(row_event(STDB_TABLE_AVATAR_MOTION, STDB_ROW_DELETE, previous_avatar));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, cart));
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_DELETE, previous_cart));
    client.update(state);
    REQUIRE(state.online.players.at(42).motion);
    CHECK(state.online.players.at(42).motion->client_time == 1.0);
    REQUIRE(state.online.carts.count(42) == 1U);
    CHECK(state.online.carts.at(42).driver == 42U);
    CHECK(state.online.carts.at(42).passenger == 43U);

    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_DELETE, cart));
    cart.golf_cart.passenger = 0;
    cart.golf_cart.condition = static_cast<std::uint8_t>(cart_condition::on_left_side);
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, cart));
    client.update(state);
    REQUIRE(state.online.carts.count(42) == 1U);
    CHECK(state.online.carts.at(42).passenger == 0U);
    CHECK(state.online.carts.at(42).condition == cart_condition::on_left_side);
}

TEST_CASE("sync: motion becomes sent only at dispatch, and remains due after disconnect") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.play = play_mode::online;
    send_motion_now(state);
    CHECK(!state.motion_sync.last_sent);
    CHECK(state.motion_sync.history.empty());
    client.update(state);
    REQUIRE(state.motion_sync.last_sent);
    REQUIRE(state.motion_sync.history.size() == 1);
    const double id = state.motion_sync.last_sent->client_time;
    send_motion_now(state);
    send_motion_now(state);
    client.update(state);
    REQUIRE(state.motion_sync.history.size() == 3);
    CHECK(state.motion_sync.history[1].motion.client_time > id);
    CHECK(state.motion_sync.history[2].motion.client_time > state.motion_sync.history[1].motion.client_time);
    stdb_event lost = fake_event(STDB_EVENT_DISCONNECTED);
    lost.retrying = true;
    queue_event(lost);
    send_motion_now(state);
    client.update(state);
    CHECK(!state.motion_sync.last_sent);
    CHECK(state.motion_sync.history.empty());
    CHECK(state.net_commands.empty());
}

TEST_CASE("sync: a failed room subscription leaves no partial state and ends the session") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    client.update(state);
    stdb_row row{};
    row.golf_cart = stdb_golf_cart{42, 7, 1.0f, 2.0f, 3.0f, 0.0f, 0, 0, 0, 0.0};
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, row));
    stdb_event failed = fake_event(STDB_EVENT_SUBSCRIPTION_FAILED);
    failed.subscription = 5;
    failed.text = fake_text("subscription_failed: test");
    queue_event(failed);
    client.update(state);
    CHECK(state.online.carts.empty());
    CHECK(!state.online.room_ready);
    CHECK(client.status() == net_status::failed);
}

TEST_CASE("sync: a bridge send failure never marks a motion sent or delays its retry") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.play = play_mode::online;
    fake_bridge().connected = false;
    send_motion_now(state);
    client.update(state);
    CHECK(!state.motion_sync.last_sent);
    CHECK(state.motion_sync.history.empty());
    CHECK(!state.motion_sync.queued);
    sync_motion(state, 0.001f);
    REQUIRE(!state.net_commands.empty());
    fake_bridge().connected = true;
    client.update(state);
    CHECK(state.motion_sync.last_sent.has_value());
    CHECK(state.motion_sync.history.size() == 1);
}


TEST_CASE("paced and crouched fixed steps survive the network bridge") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    state.play = play_mode::online;
    fake_bridge().calls.clear();
    net_command command;
    command.type = net_command_type::motion;
    command.first_step = 51;
    command.last_step = 58;
    for (const int style : {0, 1, 2, 3}) {
        move_input input;
        input.forward = -1;
        input.side = 1;
        input.paced = (style & 1) != 0;
        input.crouched = (style & 2) != 0;
        input.yaw = quantise_yaw(0.5f);
        command.runs.push_back(move_run{input, 2});
    }
    state.net_commands.push_back(command);
    client.update(state);
    REQUIRE(fake_bridge().calls.size() == 1U);
    const auto& call = fake_bridge().calls.front();
    CHECK(call.name == "stdb_update_motion");
    CHECK(call.first_step == 51);
    REQUIRE(call.runs.size() == command.runs.size());
    for (std::size_t i = 0; i < call.runs.size(); ++i) {
        CHECK(call.runs[i].paced == command.runs[i].input.paced);
        CHECK(call.runs[i].crouched == command.runs[i].input.crouched);
        CHECK(call.runs[i].steps == 2);
        CHECK(call.runs[i].forward == -1);
        CHECK(call.runs[i].side == 1);
        CHECK(call.runs[i].yaw == command.runs[i].input.yaw);
    }
}

TEST_CASE("scorecard: room snapshots preserve my account name and replace previous room scores") {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    state.round = complete_hole(*state.round, 0, 4);
    REQUIRE(tee_up(state, 1, false));
    state.ball->strokes = 2;

    const std::string name = "ME";
    const std::string other = "OTHER";
    stdb_row row{};
    row.player = stdb_player{42, fake_text(name), true, 0};
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_UPDATE, row));
    // The self player row can precede the room subscription; its duplicate
    // in the room join is not guaranteed to arrive again.
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    row = stdb_row{};
    const std::vector<std::int32_t> my_scores{4, 0, 0};
    row.room_member = stdb_room_member{42, 7, 9, my_scores.data(), my_scores.size(), true};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, row));
    const std::vector<std::int32_t> scored{4, 0, 3};
    row.room_member = stdb_room_member{43, 7, 9, scored.data(), scored.size(), false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.player = stdb_player{43, fake_text(other), true, 0};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, row));
    row = stdb_row{}; row.ball.account_id = 42; row.ball.room_id = 7; row.ball.hole = 1;
    row.ball.stroke_count = 2;
    queue_event(row_event(STDB_TABLE_BALL, STDB_ROW_INSERT, row));
    queue_event(applied(5));
    client.update(state);
    REQUIRE(state.online.room_ready);
    adopt_online_arrival(state);
    scorecard_data card = build_scorecard_data(state, shipped_text_assets().strings);
    REQUIRE(card.players.size() == 2U);
    CHECK(card.players[0].name == "ME");
    CHECK(card.players[0].total_strokes == 4);
    REQUIRE(card.players[0].current_hole);
    CHECK(card.players[0].current_hole->index == 1U);
    CHECK(card.players[0].current_hole->strokes == 2);
    CHECK(card.players[1].total_strokes == 7);

    row = stdb_row{};
    row.room_member = stdb_room_member{42, 8, 9, nullptr, 0, false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_UPDATE, row));
    const std::vector<std::int32_t> blank(3, 0);
    row.room_member = stdb_room_member{43, 8, 9, blank.data(), blank.size(), false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, row));
    // A late row from the previous subscription cannot fill this card.
    row.room_member = stdb_room_member{43, 7, 9, scored.data(), scored.size(), false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_UPDATE, row));
    client.update(state);
    CHECK(!state.online.room_ready);
    CHECK(state.online.players.empty());
    queue_event(applied(6));
    client.update(state);
    REQUIRE(state.online.room_ready);
    card = build_scorecard_data(state, shipped_text_assets().strings);
    REQUIRE(card.players.size() == 2U);
    CHECK(card.players[0].name == "ME");
    CHECK(card.players[1].total_strokes == 0);
    CHECK((card.players[1].strokes == std::vector<int>{0, 0, 0}));
}

TEST_CASE("tee book rows: scoped views, full snapshot and refresh acknowledgement") {
    game_state state = started_hole();
    auto client = connected_client(state, 42);
    const auto& queries = fake_bridge().subscriptions.at(3);
    CHECK(any_query_contains(queries, "FROM my_tee_reservation"));
    CHECK(any_query_contains(queries, "FROM my_tee_slots"));
    CHECK(any_query_contains(queries, "FROM my_tee_book_status"));
    CHECK(!(any_query_contains(queries, "FROM tee_slot")));
    CHECK(!(any_query_contains(queries, "FROM tee_reservation")));
    net_command open;
    open.type = net_command_type::open_tee_book;
    open.course_id = "first";
    state.net_commands.push_back(open);
    client.update(state);
    stdb_row slot{};
    slot.tee_slot = stdb_tee_slot{2, fake_text("first"), 1, 7, 1800000000, 3};
    queue_event(row_event(STDB_TABLE_TEE_SLOT, STDB_ROW_INSERT, slot));
    stdb_row status{};
    status.tee_book_status = stdb_tee_book_status{fake_text("first"), 1000000000};
    queue_event(row_event(STDB_TABLE_MY_TEE_BOOK_STATUS, STDB_ROW_INSERT, status));
    client.update(state);
    CHECK(!(state.online.book_ready));
    CHECK(state.online.tee_slots.empty()); // self subscription not yet complete
    queue_event(applied(4));
    client.update(state);
    REQUIRE(state.online.book_ready);
    REQUIRE(state.online.tee_slots.size() == 1);
    CHECK(state.online.tee_slots.at(2).booked_count == 3);
    CHECK(state.online.server_now >= 1000000000);
    state.net_commands.push_back(open);
    client.update(state);
    CHECK(!(state.online.book_ready));
    queue_event(row_event(STDB_TABLE_MY_TEE_BOOK_STATUS, STDB_ROW_INSERT, status)); // stale acknowledgement
    client.update(state);
    CHECK(!(state.online.book_ready));
    status.tee_book_status.server_now += 100;
    queue_event(row_event(STDB_TABLE_MY_TEE_BOOK_STATUS, STDB_ROW_INSERT, status));
    client.update(state);
    CHECK(state.online.book_ready);
}
TEST_CASE("tee reservation rows: replacement eviction, completion, teardown and reconnect") {
    game_state state = started_hole();
    auto client = connected_client(state, 42);
    stdb_row booking{};
    booking.tee_reservation = stdb_tee_reservation{9, 3, 0, true, false, 1, fake_text("first"), 1, 7, 1800000000};
    queue_event(row_event(STDB_TABLE_MY_TEE_RESERVATION, STDB_ROW_INSERT, booking));
    client.update(state);
    CHECK(!(state.online.reservation));
    queue_event(applied(4));
    client.update(state);
    REQUIRE(state.online.reservation);
    CHECK(state.online.reservation->course_id == "first");
    CHECK(state.online.reservation->scheduled_at == 1800000000);
    auto changed = booking;
    changed.tee_reservation.revision = 2; // completed score update, otherwise same visible status
    queue_event(row_event(STDB_TABLE_MY_TEE_RESERVATION, STDB_ROW_INSERT, changed));
    queue_event(row_event(STDB_TABLE_MY_TEE_RESERVATION, STDB_ROW_DELETE, booking));
    client.update(state);
    REQUIRE(state.online.reservation);
    CHECK(state.online.reservation->revision == 2);
    changed.tee_reservation.completed = true;
    changed.tee_reservation.revision = 3;
    queue_event(row_event(STDB_TABLE_MY_TEE_RESERVATION, STDB_ROW_INSERT, changed));
    client.update(state);
    REQUIRE(state.online.reservation);
    CHECK(state.online.reservation->completed);
    queue_event(fake_event(STDB_EVENT_DISCONNECTED));
    client.update(state);
    CHECK(!(state.online.reservation));
    CHECK(state.online.tee_slots.empty());
    net_command bus;
    bus.type = net_command_type::take_bus;
    bus.reservation_id = 9;
    state.net_commands.push_back(bus);
    fake_bridge().calls.clear();
    client.update(state);
    CHECK(state.net_commands.empty());
    const auto calls = fake_bridge().call_names();
    CHECK(std::find(calls.begin(), calls.end(), "stdb_take_bus") == calls.end());
    // Reconnect must handshake and await a new complete snapshot; no bus replay.
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    stdb_row account{};
    account.player.account_id = 42;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, account));
    queue_event(row_event(STDB_TABLE_MY_TEE_RESERVATION, STDB_ROW_INSERT, changed));
    client.update(state);
    CHECK(!(state.online.reservation));
    queue_event(applied(static_cast<std::uint32_t>(fake_bridge().subscriptions.size())));
    client.update(state);
    REQUIRE(state.online.reservation);
    CHECK(state.online.reservation->reservation_id == 9);
}
TEST_CASE("tee commands from another room are discarded before dispatch") {
    game_state state = started_hole();
    auto client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    queue_event(applied(5));
    client.update(state);
    net_command stale;
    stale.type = net_command_type::take_bus;
    stale.room_id = 6;
    stale.reservation_id = 8;
    state.net_commands.push_back(stale);
    fake_bridge().calls.clear();
    client.update(state);
    const auto names = fake_bridge().call_names();
    CHECK(std::find(names.begin(), names.end(), "stdb_take_bus") == names.end());
    CHECK(state.net_commands.empty());
}

TEST_CASE("dap C transport is room scoped and refusal or room teardown releases pending") {
    game_state state=started_hole(); state.play=play_mode::online; auto client=connected_client(state,42);
    queue_event(member_row(42,7,STDB_ROW_INSERT)); client.update(state);
    CHECK(any_query_contains(fake_bridge().subscriptions.back(),"FROM dap_pair WHERE room_id = 7"));
    net_command command; command.type=net_command_type::dap; command.room_id=7; command.target_account=43;
    fake_bridge().calls.clear(); state.net_commands.push_back(command); client.update(state);
    REQUIRE(fake_bridge().calls.size()==1); CHECK(fake_bridge().calls[0].name=="stdb_dap_up"); CHECK(fake_bridge().calls[0].number==43);
    state.dap_pending=43;
    auto refusal=fake_event(STDB_EVENT_REDUCER_FAILED); refusal.reducer=fake_text("dap"); refusal.text=fake_text("not_available");
    queue_event(refusal); client.update(state); update_online_play(state,0); CHECK(!(state.dap_pending));
    state.dap_pending=43; state.net_commands.push_back(command);
    queue_event(member_row(42,8,STDB_ROW_UPDATE)); client.update(state);
    CHECK(!(state.dap_pending)); CHECK(state.net_commands.empty()); CHECK(state.online.daps.empty());
    fake_bridge().calls.clear(); state.net_commands.push_back(command); client.update(state); CHECK(fake_bridge().calls.empty());
}

TEST_CASE("practice drop crosses the paired C bridge with its originating room and position") {
    auto state = started_game(fixture_hub_course()); state.play = play_mode::online;
    auto client = connected_client(state, 42);
    stdb_row member{}; member.room_member = stdb_room_member{42, 7, 0, nullptr, 0, false};
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, member)); client.update(state);
    net_command drop; drop.type = net_command_type::place_practice_ball; drop.room_id = 7; drop.placed = {3,4,5};
    state.net_commands.push_back(drop); client.update(state);
    const auto& calls = fake_bridge().calls;
    const auto found = std::find_if(calls.begin(), calls.end(), [](const auto& call) { return call.name == "stdb_place_practice_ball"; });
    REQUIRE(found != calls.end()); CHECK(found->number == 7);
    CHECK(found->position[0] == 3); CHECK(found->position[1] == 4); CHECK(found->position[2] == 5);
}

TEST_CASE("a practice drop refused through the bridge releases the pending action") {
    auto state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.kind = place_kind::hub;
    auto client = connected_client(state, 42);
    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    queue_event(applied(5));
    client.update(state);
    REQUIRE(state.online.room_ready);
    request_practice_drop(state);
    REQUIRE(state.hub_flow.ball_request);
    client.update(state);
    auto refusal = fake_event(STDB_EVENT_REDUCER_FAILED);
    refusal.reducer = fake_text(reducer_place_practice_ball);
    refusal.text = fake_text(error_not_available);
    queue_event(refusal);
    client.update(state);
    update_online_play(state, 0);
    CHECK(!state.hub_flow.ball_request);
    CHECK(!state.ball);
    request_practice_drop(state);
    CHECK(state.hub_flow.ball_request == net_command_type::place_practice_ball);
}
