#include "doctest.h"

#include "core/online_session.h"
#include "fake_stdb_bridge.h"
#include "game/ball_hit_rules.h"
#include "game/body_contact.h"
#include "game/fallen_body.h"
#include "game/knockout.h"
#include "game/movement_step.h"
#include "game/net_types.h"
#include "game/save_data.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "game/text_ids.h"
#include "net/net_client.h"
#include "physics/vector_math.h"

#include "knockout_rules.h"
#include "movement_rules.h"
#include "test_support.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>

namespace {
constexpr std::int64_t minute_ms = 60000;

const knockout_tuning& knockout_numbers() {
    return shipped_content().tuning.knockout;
}

fallen_body body_at(const glm::vec3& position, const float yaw, const std::string& place = {},
                    const std::int64_t fell_at_ms = 1000000) {
    fallen_body lying;
    lying.place_id = place;
    lying.position = position;
    lying.yaw = yaw;
    return body_fallen_at(lying, fell_at_ms, knockout_numbers());
}

// A ball `height` metres over the ground, flying at `speed` m/s along +X from 10 m before `feet`, past what the snapshot holds.
shot_result fly_at(game_state& state, const glm::vec3& feet, const std::vector<snapshot_entry>& snapshot, const float height,
                   const float speed) {
    shot_course course = current_shot_course(state);
    const std::vector<solid> solids = snapshot_solids(snapshot, state.tuning);
    course.dynamic.insert(course.dynamic.end(), solids.begin(), solids.end());
    ball_state ball = still_ball(state);
    ball.position = feet + glm::vec3(-10.0f, height, 0.0f);
    ball.velocity = glm::vec3(speed / state.tuning.scale.meters_per_world_unit, 0.0f, 0.0f);
    return simulate_ball(ball, course, state.tuning);
}

glm::vec3 ground_feet(const game_state& state, const glm::vec3& near_to) {
    return resting_on_terrain(state, near_to) - glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f);
}

bool has_body_solid(const std::vector<solid>& solids) {
    return std::any_of(solids.begin(), solids.end(), [](const solid& thing) { return thing.kind == solid_kind::body; });
}
}

TEST_CASE("a body lies for the tuning's hour and is gone at exactly 60 minutes") {
    CHECK(knockout_numbers().body_minutes == 60.0f);
    const fallen_body lying = body_at(glm::vec3(0.0f), 0.0f);
    CHECK(lying.fell_at_ms == 1000000);
    CHECK(lying.expires_at_ms == 1000000 + 60 * minute_ms);
    CHECK(!body_expired(lying, lying.fell_at_ms));
    CHECK(!body_expired(lying, lying.fell_at_ms + 60 * minute_ms - 1));  // 59:59.999
    CHECK(body_expired(lying, lying.fell_at_ms + 60 * minute_ms));       // 60:00
    CHECK(body_expired(lying, lying.fell_at_ms + 61 * minute_ms));
    CHECK(bodies_lying({lying}, lying.fell_at_ms + 60 * minute_ms - 1).size() == 1U);
    CHECK(bodies_lying({lying}, lying.fell_at_ms + 60 * minute_ms).empty());
}

TEST_CASE("a lying body is a low box a person long that stops balls, walkers and carts") {
    const game_tuning& tuning = shipped_content().tuning;
    const solid box = body_solid(body_at(glm::vec3(2.0f, 3.0f, 4.0f), 0.0f), tuning);
    CHECK(box.shape == solid_shape::box);
    CHECK(box.kind == solid_kind::body);
    CHECK(box.stops_ball);
    CHECK(box.stops_walker);
    CHECK(box.stops_cart);
    CHECK(!box.walkable);
    CHECK(near(2.0f * box.half_length, tuning.rangefinder.person_height));
    CHECK(near(2.0f * box.half_width, 0.5f));
    CHECK(near(box.height, 0.25f));
    CHECK(near(box.along, glm::vec3(0.0f, 0.0f, 1.0f), 0.0001f));  // yaw 0 lies along +Z, head that way
    CHECK(near(box.base, glm::vec3(2.0f, 3.0f, 4.0f)));
    CHECK(box.height < 0.2f * tuning.rangefinder.person_height);  // a standing person is far taller
}

TEST_CASE("a ball into a lying body stops short of it and hits no one; over it, the ball flies on") {
    game_state state = started_hole();
    still_air(state);
    const glm::vec3 feet = ground_feet(state, state.ball->position + glm::vec3(10.0f, 0.0f, 0.0f));
    const std::vector<snapshot_entry> snapshot{body_snapshot_entry(body_at(feet, 0.0f))};

    const shot_result low = fly_at(state, feet, snapshot, 0.12f, 20.0f);
    CHECK(low.person_hits.empty());
    CHECK(std::any_of(low.events.begin(), low.events.end(),
                      [](const shot_event& event) { return event.kind == shot_event_kind::board_hit; }));
    CHECK(std::none_of(low.events.begin(), low.events.end(),
                       [](const shot_event& event) { return event.kind == shot_event_kind::person_hit; }));
    CHECK(low.rest_position.x < feet.x);  // stopped short, turned back

    const shot_result open = fly_at(state, feet, {}, 0.12f, 20.0f);
    CHECK(open.rest_position.x > feet.x + 2.0f);

    // Well above the box it passes over, as over any low thing, and lands beyond it.
    const shot_result over = fly_at(state, feet, snapshot, 3.0f, 20.0f);
    CHECK(over.rest_position.x > feet.x);
    CHECK(over.person_hits.empty());
}

TEST_CASE("only standing people are hit: a body in a snapshot is never a consequence, whatever the speed") {
    const game_tuning& tuning = shipped_content().tuning;
    shot_result result;
    result.person_hits = {person_hit{0, 0.3f, 60.0f, 0.2f}};
    const std::vector<snapshot_entry> snapshot{snapshot_entry{snapshot_kind::body, glm::vec3(0.0f), 0.0f, false, 91}};
    for (std::uint64_t seed = 0; seed < 200; ++seed) {
        CHECK(hit_consequences(result, snapshot, tuning, seed).empty());
    }
}

TEST_CASE("a lying body blocks walking in the game, offline and online") {
    game_state state = started_game(fixture_hub_course());
    state.local_unix_ms = 5000000;
    const glm::vec3 start = state.player.position;
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    const auto walk = [](game_state walker) {
        game_input forward;
        forward.forward_held = true;
        for (int i = 0; i < 150; ++i) {
            update_game(walker, forward, 1.0f / 60.0f);
        }
        return walker;
    };
    const float free_distance = horizontal_distance(walk(state).player.position, start);
    REQUIRE(free_distance > 4.0f);

    // Offline: a body of mine in this place, across the way 1.5 m ahead.
    glm::vec3 spot = start + ahead * 1.5f;
    spot.y = terrain_height(state.area, spot);
    const fallen_body lying = body_at(spot, state.player.yaw + glm::half_pi<float>(), state.place.id, state.local_unix_ms - 1000);
    state.save.bodies = {lying};
    REQUIRE(bodies_in_world(state).size() == 1U);
    CHECK(horizontal_distance(walk(state).player.position, start) < 1.5f);
    // The same body of another place, or expired, is no obstacle.
    state.save.bodies[0].place_id = "elsewhere";
    CHECK(bodies_in_world(state).empty());
    CHECK(horizontal_distance(walk(state).player.position, start) > 4.0f);
    state.save.bodies[0].place_id = state.place.id;
    state.local_unix_ms = lying.expires_at_ms;
    CHECK(bodies_in_world(state).empty());

    // Online: the room's body rows.
    game_state online = started_game(fixture_hub_course());
    online.play = play_mode::online;
    online.local_unix_ms = 5000000;
    online.online.status = net_status::connected;
    online.online.account_id = 1;
    online.online.room_ready = true;
    online.online.carts[1] = room_cart{online.cart.pose.position, online.cart.pose.yaw, 0};
    online.online.bodies[9] = lying;
    CHECK(bodies_in_world(online).size() == 1U);
    CHECK(horizontal_distance(walk(online).player.position, start) < 1.5f);
    CHECK(has_body_solid(walking_obstacles(online)));
    CHECK(has_body_solid(cart_obstacles(online)));
}

TEST_CASE("a lying body stops the server's walking steps and is among its carts' obstacles") {
    game_state state = started_game(fixture_hub_course());
    const game_tuning& tuning = state.tuning;
    const glm::vec3 start = state.player.position;
    const float yaw = 0.7f;
    glm::vec3 spot = start + yaw_direction(yaw) * 1.5f;
    spot.y = terrain_height(state.area, spot);
    const std::vector<fallen_body> lying{body_at(spot, yaw + glm::half_pi<float>())};

    move_world world{state.area, state.world.cart_roads, tuning};
    world.walk_speed = tuning.player.walk_speed;
    world.spec = my_cart_spec(state);
    world.limits = movement_limits_for(tuning, world.spec, {}, 0);
    mover_state joined = placed_mover(mover_state{}, start);
    joined.credit_seconds = tuning.server.max_motion_gap_seconds;
    net_motion motion;
    motion.mode = motion_mode::walk;
    motion.position = start;
    motion.client_time = 5.0;
    move_input forward;
    forward.forward = 1;
    forward.yaw = quantise_yaw(yaw);
    const move_request request{motion, 1, start, {move_run{forward, 90}}};  // 0.75 s: 3.75 m on foot

    world.walker_obstacles = walker_obstacles({}, state.area.cups, {}, tuning);
    const move_outcome free = play_move(joined, std::nullopt, 1, request, 1.0f, world);
    REQUIRE(free.error.empty());
    CHECK(horizontal_distance(free.mover.position, start) > 3.0f);

    world.walker_obstacles = walker_obstacles({}, state.area.cups, {}, tuning, lying);
    const move_outcome blocked = play_move(joined, std::nullopt, 1, request, 1.0f, world);
    REQUIRE(blocked.error.empty());
    CHECK(horizontal_distance(blocked.mover.position, start) < 1.5f);

    CHECK(has_body_solid(cart_obstacles_of({}, {}, state.area.cups, {}, tuning, lying)));
    CHECK(!has_body_solid(cart_obstacles_of({}, {}, state.area.cups, {}, tuning)));
}

TEST_CASE("knocking me out offline leaves my body in the save for an hour and sends me to the menu") {
    game_state state = started_hole();
    state.favourite_colour = 3;
    state.hand = handedness::left;
    state.player.yaw = 1.2f;
    state.bag.where = bag_where::on_back;
    const std::int64_t now = 7000000;
    state.local_unix_ms = now;
    knock_out_offline(state, now);
    CHECK(state.knocked_out);
    CHECK(state.save_requested);
    REQUIRE(state.save.bodies.size() == 1U);
    const fallen_body& mine = state.save.bodies[0];
    CHECK(mine.place_id == state.place.id);
    CHECK(near(mine.position.x, state.player.position.x));
    CHECK(near(mine.position.z, state.player.position.z));
    CHECK(near(mine.yaw, 1.2f));
    CHECK(mine.bag_on_back);
    CHECK(mine.colour == 3);
    CHECK(mine.hand == handedness::left);
    CHECK(mine.account == 0U);
    CHECK(mine.fell_at_ms == now);
    CHECK(mine.expires_at_ms == now + 60 * minute_ms);
    CHECK(bodies_in_world(state).size() == 1U);

    // A later knockout drops the bodies that have expired.
    state.local_unix_ms = now + 61 * minute_ms;
    CHECK(bodies_in_world(state).empty());
    knock_out_offline(state, now + 61 * minute_ms);
    CHECK(state.save.bodies.size() == 1U);
}

TEST_CASE("bodies in the save round trip, the version is 10, and an older save reads with none") {
    CHECK(current_save_version == 10);
    save_data save;
    fallen_body lying = body_at(glm::vec3(1.5f, 2.25f, -3.0f), 0.5f, "practice_ground", 123456789012LL);
    lying.bag_on_back = true;
    lying.colour = 4;
    lying.hand = handedness::left;
    save.bodies = {lying, body_at(glm::vec3(0.0f), 0.0f, "other_course", 5)};
    const std::optional<save_data> read = parse_save_data(save_data_to_json(save));
    REQUIRE(read.has_value());
    CHECK(read->version == 10);
    REQUIRE(read->bodies.size() == 2U);
    CHECK(read->bodies[0].place_id == "practice_ground");
    CHECK(near(read->bodies[0].position, lying.position));
    CHECK(near(read->bodies[0].yaw, 0.5f));
    CHECK(read->bodies[0].fell_at_ms == 123456789012LL);
    CHECK(read->bodies[0].expires_at_ms == lying.expires_at_ms);
    CHECK(read->bodies[0].bag_on_back);
    CHECK(read->bodies[0].colour == 4);
    CHECK(read->bodies[0].hand == handedness::left);
    CHECK(read->bodies[1].place_id == "other_course");

    // Expired bodies are dropped when used: the second fell long before.
    CHECK(bodies_lying(read->bodies, 123456789012LL + 60 * minute_ms - 1).size() == 1U);
    CHECK(bodies_lying(read->bodies, 123456789012LL + 60 * minute_ms).empty());

    const std::optional<save_data> old = parse_save_data(R"({"version": 9, "holes_completed": 2})");
    REQUIRE(old.has_value());
    CHECK(old->version == 10);
    CHECK(old->bodies.empty());
    // A body that does not read whole is dropped, never the save.
    const std::optional<save_data> broken =
        parse_save_data(R"({"version": 10, "holes_completed": 2, "bodies": [{"place": "x"}, 7]})");
    REQUIRE(broken.has_value());
    CHECK(broken->holes_completed == 2);
    CHECK(broken->bodies.empty());
}

TEST_CASE("a knockout lands on someone still in the room, and leaves a body for the hour where they stood") {
    CHECK(knockout_lands(7, 7, true));
    CHECK(!knockout_lands(std::nullopt, 7, true));  // gone
    CHECK(!knockout_lands(8, 7, true));             // in another room
    CHECK(!knockout_lands(7, 7, false));            // no known position
    knocked_golfer golfer;
    golfer.account = 91;
    golfer.position = glm::vec3(4.0f, 1.0f, 2.0f);
    golfer.yaw = 2.0f;
    golfer.bag_on_back = true;
    golfer.colour = 5;
    golfer.hand = handedness::left;
    const fallen_body lying = body_after_knockout(golfer, 9000, knockout_numbers());
    CHECK(lying.account == 91U);
    CHECK(near(lying.position, glm::vec3(4.0f, 1.0f, 2.0f)));
    CHECK(near(lying.yaw, 2.0f));
    CHECK(lying.bag_on_back);
    CHECK(lying.colour == 5);
    CHECK(lying.hand == handedness::left);
    CHECK(lying.fell_at_ms == 9000);
    CHECK(lying.expires_at_ms == 9000 + 60 * minute_ms);
}

namespace {
online_session signed_in_session(game_state& state, const std::uint64_t account, const std::uint64_t room) {
    fake_bridge().reset();
    online_session session(*net_client::create(fake_online_config(), "signed in", "failed", shipped_content().content_hash));
    session.carry_out({online_request{online_request_type::begin_silent_login, {}}}, state);
    fake_bridge().connected = true;
    queue_event(fake_event(STDB_EVENT_SIGNED_IN));
    queue_event(fake_event(STDB_EVENT_CONNECTED));
    queue_event(server_protocol_event(protocol_version));
    queue_event(server_content_event(shipped_content().content_hash));
    stdb_row mine{};
    mine.player.account_id = account;
    queue_event(row_event(STDB_TABLE_MY_ACCOUNT, STDB_ROW_INSERT, mine));
    session.update(state);
    stdb_row member{};
    member.room_member.account_id = account;
    member.room_member.room_id = room;
    queue_event(row_event(STDB_TABLE_ROOM_MEMBER, STDB_ROW_INSERT, member));
    session.update(state);
    stdb_event applied = fake_event(STDB_EVENT_SUBSCRIPTION_APPLIED);
    applied.subscription = static_cast<std::uint32_t>(fake_bridge().subscriptions.size());
    queue_event(applied);
    session.update(state);
    state.play = play_mode::online;
    return session;
}

stdb_event body_row(const std::uint64_t id, const std::uint64_t account, const std::uint64_t room, const stdb_row_change change) {
    stdb_row row{};
    row.body = stdb_body{id, account, room, 4.0f, 5.0f, 6.0f, 0.5f, 1000000, 1000000 + 3600LL * 1000000, true, 3, 1};
    return row_event(STDB_TABLE_BODY, change, row);
}
}

TEST_CASE("the room's body rows are adopted with the room and outlive their owner leaving and coming back") {
    game_state state = started_game(fixture_hub_course());
    online_session session = signed_in_session(state, 42, 7);
    REQUIRE(state.online.room_ready);
    const std::vector<std::string>& room_queries = fake_bridge().subscriptions.back();
    CHECK(std::any_of(room_queries.begin(), room_queries.end(),
                      [](const std::string& query) { return query == "SELECT * FROM body WHERE room_id = 7"; }));

    queue_event(body_row(1, 91, 7, STDB_ROW_INSERT));
    queue_event(body_row(2, 92, 9, STDB_ROW_INSERT));  // another room's
    session.update(state);
    REQUIRE(state.online.bodies.size() == 1U);
    const fallen_body& lying = state.online.bodies.at(1);
    CHECK(lying.account == 91U);
    CHECK(near(lying.position, glm::vec3(4.0f, 5.0f, 6.0f)));
    CHECK(near(lying.yaw, 0.5f));
    CHECK(lying.bag_on_back);
    CHECK(lying.colour == 3);
    CHECK(lying.hand == handedness::left);
    CHECK(lying.expires_at_ms - lying.fell_at_ms == 60 * minute_ms);

    // The owner (91) is in the room, leaves and comes back: the body row is not theirs to lose.
    for (const stdb_row_change change : {STDB_ROW_INSERT, STDB_ROW_DELETE, STDB_ROW_INSERT}) {
        stdb_row member{};
        member.room_member.account_id = 91;
        member.room_member.room_id = 7;
        queue_event(row_event(STDB_TABLE_ROOM_MEMBER, change, member));
        session.update(state);
        CHECK(state.online.bodies.size() == 1U);
    }
    // Only its own deletion (its expiry) removes it.
    queue_event(body_row(1, 91, 7, STDB_ROW_DELETE));
    session.update(state);
    CHECK(state.online.bodies.empty());
}

TEST_CASE("a knockout notice ends my session and returns me to the menu with the message, and I may sign in again") {
    game_state state = started_game(fixture_hub_course());
    online_session session = signed_in_session(state, 42, 7);
    REQUIRE(state.online.room_ready);

    stdb_row notice{};
    notice.knockout_notice.at_micros = 123;
    queue_event(row_event(STDB_TABLE_MY_KNOCKOUT_NOTICE, STDB_ROW_INSERT, notice));
    session.update(state);
    const std::vector<std::string> calls = fake_bridge().call_names();
    CHECK(std::find(calls.begin(), calls.end(), "stdb_disconnect") != calls.end());
    const online_round_status round = session.watch_round(state);
    CHECK(round.event == online_round_event::lost);
    CHECK(round.message_key == text_menu_knocked_out);
    // It is no sign-in failure: the menus see someone who may sign in at once.
    const online_menu_status status = session.menu_status(state, std::chrono::system_clock::now());
    CHECK(status.failure_id.empty());
    CHECK(status.status == net_status::failed);
    session.carry_out({online_request{online_request_type::begin_silent_login, {}}}, state);
    CHECK(session.menu_status(state, std::chrono::system_clock::now()).status != net_status::failed);
}

TEST_CASE("the knockout message is a string in the table") {
    CHECK(lookup_text(shipped_text_assets(), text_menu_knocked_out) == "YOU WERE KNOCKED OUT.");
}
