#include "doctest.h"

#include "fake_stdb_bridge.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/progression.h"
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
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
    client.begin_login();
    fake_bridge().connected = true;
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    stdb_row row{};
    row.player.account_id = account_id;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, row));
    client.update(state);
    return client;
}

stdb_event skill_row(const std::uint64_t id, const std::uint64_t account_id, const std::string& skill, const int xp,
                     const stdb_row_change change = STDB_ROW_INSERT) {
    stdb_row row{};
    row.player_skill = stdb_player_skill{id, account_id, fake_text(skill), xp};
    return row_event(STDB_TABLE_PLAYER_SKILL, change, row);
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
    CHECK(!net_client::create(fake_online_config(), "signed in", "failed").has_value());
    fake_bridge().reset();
    CHECK(net_client::create(fake_online_config(), "signed in", "failed").has_value());
    REQUIRE(!fake_bridge().calls.empty());
    CHECK(fake_bridge().calls[0].text == "http://localhost:3000");
    CHECK(fake_bridge().calls[0].number == 1);  // anonymous
}

TEST_CASE("signing in moves through the statuses, and failures say why") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
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
    client.update(state);
    CHECK(client.status() == net_status::connected);
    CHECK(client.failure().empty());
    CHECK(!client.guest());
}

TEST_CASE("an anonymous sign-in is a guest") {
    fake_bridge().reset();
    game_state state = started_hole();
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
    client.begin_login();
    stdb_event signed_in = fake_event(STDB_EVENT_SIGNED_IN);
    signed_in.login_method = STDB_LOGIN_ANONYMOUS;
    queue_event(signed_in);
    client.update(state);
    CHECK(client.guest());
}

TEST_CASE("subscriptions go from the protocol to my views to my rows to my room, never a whole table") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    std::vector<std::vector<std::string>>& subscriptions = fake_bridge().subscriptions;
    REQUIRE(subscriptions.size() == 3U);
    CHECK(subscriptions[0] == std::vector<std::string>{"SELECT * FROM server_protocol"});
    CHECK(any_query_contains(subscriptions[1], "my_account"));
    CHECK(any_query_contains(subscriptions[2], "FROM player_skill WHERE account_id = 42"));
    CHECK(any_query_contains(subscriptions[2], "FROM room_member WHERE account_id = 42"));
    // The score history stays on the server: nothing shows it.
    CHECK(!any_query_contains(subscriptions[2], "hole_score"));

    queue_event(member_row(42, 7, STDB_ROW_INSERT));
    client.update(state);
    REQUIRE(subscriptions.size() == 4U);
    CHECK(any_query_contains(subscriptions[3], "FROM avatar_motion WHERE room_id = 7"));
    CHECK(any_query_contains(subscriptions[3], "FROM shot_event WHERE room_id = 7"));
    CHECK(any_query_contains(subscriptions[3], "FROM golf_cart WHERE room_id = 7"));
    CHECK(any_query_contains(subscriptions[3], "room_member.room_id = 7"));

    // Someone else's row changes nothing; my move to another room swaps the subscription.
    queue_event(member_row(43, 9, STDB_ROW_INSERT));
    client.update(state);
    CHECK(subscriptions.size() == 4U);
    queue_event(member_row(42, 8, STDB_ROW_UPDATE));
    client.update(state);
    REQUIRE(subscriptions.size() == 5U);
    CHECK(fake_bridge().unsubscribed == std::vector<std::uint32_t>{4});
    CHECK(any_query_contains(subscriptions[4], "WHERE room_id = 8"));

    queue_event(member_row(42, 8, STDB_ROW_DELETE));
    client.update(state);
    const std::vector<std::uint32_t> both{4, 5};
    CHECK(fake_bridge().unsubscribed == both);

    // The one-row protocol view is the only query without a WHERE.
    for (std::size_t i = 1; i < subscriptions.size(); ++i) {
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
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
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
    const std::uint32_t self = 3;

    queue_event(skill_row(1, 42, "fitness", 500));
    queue_event(skill_row(2, 99, "fitness", 7000));  // someone else's
    client.update(state);
    CHECK(!state.online.progress_received);  // the snapshot is not complete yet

    stdb_row flag{};
    flag.world_flag = stdb_world_flag{5, 42, fake_text("found_lost_ball")};
    queue_event(row_event(STDB_TABLE_WORLD_FLAG, STDB_ROW_INSERT, flag));
    stdb_row token{};
    token.collected = stdb_collected{6, 42, fake_text("range_token"), true, 2, 3};
    queue_event(row_event(STDB_TABLE_COLLECTED, STDB_ROW_INSERT, token));
    stdb_row me{};
    me.player = stdb_player{42, fake_text("Alpha"), true, 4};
    queue_event(row_event(STDB_TABLE_PLAYER, STDB_ROW_INSERT, me));
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

    queue_event(skill_row(1, 42, "fitness", 500 + state.tuning.xp_drops.min_visible_xp, STDB_ROW_UPDATE));
    client.update(state);
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].skill_id == "fitness");
}

TEST_CASE("a lost connection forgets the session's rows") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    queue_event(skill_row(1, 42, "fitness", 500));
    queue_event(applied(3));
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
    const std::uint32_t self = 3;
    queue_event(applied(self));
    client.update(state);
    REQUIRE(state.online.progress_received);

    // Claim lost_ball (accepted), then beer_can (refused): both answers arrive together.
    state.online.pending_claims = {"lost_ball", "beer_can"};
    stdb_row found{};
    found.collected = stdb_collected{7, 42, fake_text("lost_ball"), false, 1, 0};
    queue_event(row_event(STDB_TABLE_COLLECTED, STDB_ROW_INSERT, found));
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
    client.join_course("kalo_par_3");
    client.leave_room();
    client.create_link_code();
    client.redeem_link_code("ab2c-d3ef");
    const std::vector<std::string> expected{"stdb_claim_name", "stdb_join_course", "stdb_leave_room",
                                            "stdb_create_link_code", "stdb_redeem_link_code"};
    CHECK(fake_bridge().call_names() == expected);
    CHECK(fake_bridge().calls[0].text == "Alpha");
    CHECK(fake_bridge().calls[1].text == "kalo_par_3");
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
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
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
    net_client client = *net_client::create(fake_online_config(), "signed in", "failed");
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
    queue_event(applied(3));
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
    stdb_row me{};
    me.player.account_id = 42;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, me));
    client.update(state);
    REQUIRE(fake_bridge().subscriptions.size() == before + 3);
    CHECK(any_query_contains(fake_bridge().subscriptions.back(), "account_id = 42"));
}

TEST_CASE("linking this login to another account never mixes their progress") {
    game_state state = started_hole();
    state.play = play_mode::online;
    net_client client = connected_client(state, 42);
    queue_event(skill_row(1, 42, "fitness", 500));
    queue_event(applied(3));
    client.update(state);
    REQUIRE(skill_xp(state.online.progress.skills, "fitness") == 500);

    stdb_row other{};
    other.player.account_id = 77;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, other));
    client.update(state);
    CHECK(std::find(fake_bridge().unsubscribed.begin(), fake_bridge().unsubscribed.end(), 3U) != fake_bridge().unsubscribed.end());
    CHECK(!state.online.progress_received);

    queue_event(skill_row(1, 42, "fitness", 500, STDB_ROW_DELETE));  // the old account's rows leave
    queue_event(skill_row(9, 77, "golf_swing", 50));
    queue_event(applied(4));
    client.update(state);
    CHECK(skill_xp(state.online.progress.skills, "fitness") == 0);
    CHECK(skill_xp(state.online.progress.skills, "golf_swing") == 50);
    CHECK(state.xp_drops.empty());
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
    row.room_member = stdb_room_member{43, 7, 9, strokes.data(), strokes.size()};
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
    row.ball = stdb_ball{43, 7, 1, 4.0f, 5.0f, 6.0f, 2, 1500, true};
    queue_event(row_event(STDB_TABLE_BALL, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.golf_cart = stdb_golf_cart{43, 7, 10.0f, 1.0f, -3.0f, 0.75f, 42, static_cast<std::uint8_t>(cart_condition::on_left_side)};
    queue_event(row_event(STDB_TABLE_GOLF_CART, STDB_ROW_INSERT, row));
    row = stdb_row{};
    row.golf_group = stdb_golf_group{9, 7, 43, 1};
    queue_event(row_event(STDB_TABLE_GOLF_GROUP, STDB_ROW_INSERT, row));
    row = stdb_row{};
    const std::int32_t flags_out[] = {3};
    const stdb_snapshot_entry snapshot[] = {stdb_snapshot_entry{1, 4.0f, 0.0f, 5.0f, 0.5f, false},
                                            stdb_snapshot_entry{0, 1.0f, 2.0f, 3.5f, 0.0f, true}};
    row.shot_event = stdb_shot_event{7, 43, 1, stdb_shot{2, 0.25f, 0.1f, -0.4f, 0.8f, fake_text(club), 3500, flags_out, 1},
                                     1, 2, 0.5f, 99, snapshot, 2, 1.0f, 2.0f, 3.0f, 7.0f, 8.0f, 9.0f, false};
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
    CHECK(shot.record.setup.hand == handedness::left);
    CHECK(shot.record.seed == 99U);
    REQUIRE(shot.record.snapshot.size() == 2U);
    CHECK(shot.record.snapshot[0].kind == snapshot_kind::cart);
    CHECK(shot.record.snapshot[1].shooter);
    CHECK(shot.golfer.natural == natural_shape::fade);
    CHECK(shot.golfer.spread_scale == 0.5f);
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

    // My cart going over, and righting someone's.
    net_command overturn;
    overturn.type = net_command_type::overturn_cart;
    overturn.overturned_to = cart_condition::sunk;
    state.net_commands.push_back(overturn);
    net_command right;
    right.type = net_command_type::right_cart;
    right.cart_owner = 44;
    state.net_commands.push_back(right);
    fake_bridge().calls.clear();
    client.update(state);
    REQUIRE(fake_bridge().calls.size() == 2U);
    CHECK(fake_bridge().calls[0].name == "stdb_overturn_cart");
    CHECK(fake_bridge().calls[0].number == static_cast<std::int64_t>(cart_condition::sunk));
    CHECK(fake_bridge().calls[1].name == "stdb_right_cart");
    CHECK(fake_bridge().calls[1].number == 44);

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

TEST_CASE("group and give-up commands reach their reducers") {
    game_state state = started_hole();
    net_client client = connected_client(state, 42);
    for (const net_command_type type : {net_command_type::create_group, net_command_type::join_group,
                                        net_command_type::leave_group, net_command_type::give_up_hole}) {
        net_command command;
        command.type = type;
        command.group_id = 9;
        state.net_commands.push_back(command);
    }
    fake_bridge().calls.clear();
    client.update(state);
    const std::vector<std::string> names = fake_bridge().call_names();
    CHECK(std::find(names.begin(), names.end(), "stdb_create_group") != names.end());
    CHECK(std::find(names.begin(), names.end(), "stdb_leave_group") != names.end());
    CHECK(std::find(names.begin(), names.end(), "stdb_give_up_hole") != names.end());
    for (const fake_stdb_bridge::call& call : fake_bridge().calls) {
        if (call.name == "stdb_join_group") {
            CHECK(call.number == 9);
        }
    }
}
