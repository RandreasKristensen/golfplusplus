#include "doctest.h"
#include "tee_rules.h"
#include "game/server_errors.h"
#include "game/course_world_loader.h"
#include "game/json_util.h"
#include "test_support.h"

TEST_CASE("tee times: UTC quarters, full group, atomic replacement guards and stale booking ids") {
    const auto& tuning = shipped_content().tuning.server;
    const hub_clock clock{shipped_content().tuning.sky_clock, shipped_content().hub.sky.utc_offset_hours};
    const auto at = 100 * 15 * minute_micros;
    CHECK(next_tee_time(at - 1, tuning) == at);
    CHECK(next_tee_time(at, tuning) == at);
    CHECK(next_tee_time(at + 1, tuning) == at + 15 * minute_micros);
    CHECK(tuning.group_capacity == 5); // named owner departure, not real-golf foursomes
    CHECK(check_booking(7, 7, at, at - 1, 4, false, tuning, clock).empty());
    CHECK(check_booking(7, 7, at, at - 1, 5, false, tuning, clock) == error_group_full);
    CHECK(check_booking(7, 7, at, at - 1, 5, true, tuning, clock).empty());
    CHECK(check_booking(6, 7, at, at - 1, 0, false, tuning, clock) == error_stale_booking);
    CHECK(check_booking(0, 7, at, at - 1, 0, false, tuning, clock) == error_stale_booking);
    CHECK(check_booking(7, 7, at + 1, at - 1, 0, false, tuning, clock) == error_invalid_slot);
    CHECK(check_booking(7, 7, at, at + 1, 0, false, tuning, clock) == error_invalid_slot);
    CHECK(check_booking(7, 7, at + 2 * 24 * 60 * minute_micros, at - 1, 0, false, tuning, clock) == error_invalid_slot);

}
namespace {
// Six game days a real day, the hub two hours ahead of UTC: the hub's days
// begin at real 03:40, 07:40, 11:40, 15:40, 19:40 and 23:40 UTC.
hub_clock six_a_day() { return hub_clock{sky_timeline{0, 0.0, 6.0, 1989}, 2.0}; }
constexpr std::int64_t hour_micros = 60 * minute_micros;
}

TEST_CASE("tee book horizon: the end of tomorrow on the hub's clock, never a slot beyond it") {
    const auto& tuning = shipped_content().tuning.server;
    CHECK(tuning.tee_book_days_ahead == 1);
    const hub_clock clock = six_a_day();
    // 15:39 is still the hub day that began at 11:40: tomorrow ends at 19:40.
    const auto before = tee_times(15 * hour_micros + 39 * minute_micros, tuning, clock);
    REQUIRE(!before.empty());
    CHECK(before.front() == 15 * hour_micros + 45 * minute_micros);
    CHECK(before.back() == 19 * hour_micros + 30 * minute_micros);
    // One minute on, the hub day has turned: the book reaches the next midnight, 23:40.
    const auto after = tee_times(15 * hour_micros + 40 * minute_micros, tuning, clock);
    CHECK(after.front() == 15 * hour_micros + 45 * minute_micros);
    CHECK(after.back() == 23 * hour_micros + 30 * minute_micros);
    // Always between one and two in-game days (4 and 8 real hours) ahead, and on today or tomorrow only.
    for (std::int64_t now = 12 * hour_micros; now < 36 * hour_micros; now += 7 * minute_micros) {
        const auto times = tee_times(now, tuning, clock);
        REQUIRE(!times.empty());
        CHECK(times.back() - now < 8 * hour_micros);
        CHECK(times.back() - now > 4 * hour_micros - 15 * minute_micros);
        for (const auto at : times) { CHECK(hub_day_at(clock, at) - hub_day_at(clock, now) <= 1); }
        CHECK(hub_day_at(clock, times.back() + 15 * minute_micros) - hub_day_at(clock, now) == 2);
    }
}

TEST_CASE("tee book horizon: the server refuses a booking beyond tomorrow, at the same boundary the book lists") {
    const auto& tuning = shipped_content().tuning.server;
    const hub_clock clock = six_a_day();
    const std::int64_t now = 12 * hour_micros;
    CHECK(check_booking(0, 0, 19 * hour_micros + 30 * minute_micros, now, 0, false, tuning, clock).empty());
    CHECK(check_booking(0, 0, 19 * hour_micros + 45 * minute_micros, now, 0, false, tuning, clock) == error_invalid_slot);
    CHECK(check_booking(0, 0, 24 * hour_micros, now, 0, false, tuning, clock) == error_invalid_slot);
    // After the hub's midnight the next day is open.
    CHECK(check_booking(0, 0, 23 * hour_micros + 30 * minute_micros, 15 * hour_micros + 40 * minute_micros, 0, false, tuning, clock).empty());
    CHECK(check_booking(0, 0, 23 * hour_micros + 45 * minute_micros, 15 * hour_micros + 40 * minute_micros, 0, false, tuning, clock) == error_invalid_slot);
}

TEST_CASE("tee times: bus lead and per-player expiry have exact microsecond boundaries") {
    const auto& tuning = shipped_content().tuning.server;
    const auto at = 100 * 15 * minute_micros;
    const auto open = at - 15 * minute_micros;
    const auto expire = at + 15 * minute_micros;
    CHECK(check_bus(at, open - 1, false, tuning) == error_bus_too_early);
    CHECK(check_bus(at, open, false, tuning).empty());
    CHECK(check_bus(at, at - 1, false, tuning).empty());
    // The bus leaves at the tee time: a booking still in the hub is missed.
    CHECK(check_bus(at, at, false, tuning) == error_booking_expired);
    CHECK(!tee_missed(at, at - 1, false, false, tuning));
    CHECK(tee_missed(at, at, false, false, tuning));
    // A golfer who boarded has until expiry for the first strike.
    CHECK(!tee_missed(at, expire - 1, false, true, tuning));
    CHECK(tee_missed(at, expire, false, true, tuning));
    CHECK(!tee_missed(at, expire + 100, true, false, tuning));
    CHECK(tee_expired(at, expire, false, tuning));
    CHECK(!(tee_expired(at, expire, true, tuning)));
    CHECK(check_bus(at, expire + 100, true, tuning).empty()); // reconnect after actual start
}
TEST_CASE("tee times: the first strike is early only on a clear tee, never skips the first tee or misses expiry") {
    const auto& tuning = shipped_content().tuning.server;
    const auto at = 100 * 15 * minute_micros;
    CHECK(check_first_tee(at, at - 1, 0, false, false, tuning) == error_tee_too_early);
    CHECK(check_first_tee(at, at - 10 * minute_micros, 0, false, true, tuning).empty()); // the tee is clear: go early
    CHECK(check_first_tee(at, at, 1, false, false, tuning) == error_first_tee_required);
    CHECK(check_first_tee(at, at, 0, false, false, tuning).empty());
    CHECK(check_first_tee(at, at + 15 * minute_micros - 1, 0, false, false, tuning).empty());
    CHECK(check_first_tee(at, at + 15 * minute_micros, 0, false, false, tuning) == error_booking_expired);
    CHECK(check_first_tee(at, at + 1, 0, true, false, tuning) == error_stale_booking);
}
TEST_CASE("tee times: five stable tee lanes keep balls separate along the tee's left axis") {
    const auto& tuning = shipped_content().tuning;
    const glm::vec3 tee(10.0f, 2.0f, 5.0f);
    const glm::vec3 pin(10.0f, 3.0f, 100.0f);
    const float spacing = tuning.server.group_tee_spacing_meters / tuning.scale.meters_per_world_unit;
    for (std::uint32_t lane = 0; lane < 5; ++lane) {
        const auto at = booked_tee_position(tee, pin, lane, tuning);
        CHECK(near(at.x, tee.x + static_cast<float>(lane) * spacing, 0.0001f));
        CHECK(at.z == tee.z);
        CHECK(at.y == tee.y);
        for (std::uint32_t other = 0; other < lane; ++other) {
            CHECK(horizontal_distance(at, booked_tee_position(tee, pin, other, tuning)) >= spacing - 0.0001f);
        }
    }
    const auto turned = booked_tee_position(tee, tee + glm::vec3(100.0f, 0.0f, 0.0f), 1, tuning);
    CHECK(near(turned.z, tee.z - spacing, 0.0001f));
}

TEST_CASE("tee hub anchors: shared loader rejects incomplete or non-practice hub data") {
    auto course = fixture_hub_course();
    const auto text = read_text_file(fixture_root() + "/world/courses/hub/world.json");
    REQUIRE(text);
    auto root = parse_json(*text);
    REQUIRE(root);
    (*root)["hub"] = {{"book", {{"position", {0, 0, 16}}, {"reach", 2}}}};
    (*root)["bus_stop"] = {{"position", {6, 0, 16}}, {"heading", 1.5}};
    const auto world = parse_world_from_text(root->dump()).value;
    REQUIRE(world);
    REQUIRE(world->hub);
    CHECK(world->hub->book.reach == 2);
    REQUIRE(world->bus_stop);
    CHECK(world->bus_stop->position.x == 6);
    CHECK(world->bus_stop->heading == 1.5f);
    CHECK(!parse_course_world_from_text(root->dump(), course).value);
    root->erase("hub");
    CHECK(parse_course_world_from_text(root->dump(), course).value);
    (*root)["bus_stop"].erase("heading");
    CHECK(!parse_course_world_from_text(root->dump(), course).value);
}

TEST_CASE("a booking carries cart or carry, a cart only where the course has them") {
    CHECK(booked_takes_cart(true, true));
    CHECK(!booked_takes_cart(false, true));
    CHECK(!booked_takes_cart(true, false));
    CHECK(!booked_takes_cart(false, false));
}

TEST_CASE("the departure board: the next booked times whose bus has not left, earliest first, a fixed number of rows") {
    const auto& tuning = shipped_content().tuning.server;
    const auto at = 100 * 15 * minute_micros;
    // The bus leaves at the tee time: gone from that microsecond.
    CHECK(!bus_gone(at, at - 1));
    CHECK(bus_gone(at, at));
    // Two courses at one time, in slot order; later ones beyond the rows are left off.
    const std::vector<board_slot> waiting{{9, at + 45 * minute_micros}, {4, at + 15 * minute_micros}, {3, at}, {7, at + 15 * minute_micros},
                                          {5, at + 30 * minute_micros}, {8, at + 60 * minute_micros}};
    const auto board = next_departures(waiting, static_cast<std::size_t>(tuning.departure_board_rows));
    REQUIRE(board.size() == static_cast<std::size_t>(tuning.departure_board_rows));
    CHECK(tuning.departure_board_rows == 4);
    const std::vector<std::uint64_t> expected{3, 4, 7, 5};
    for (std::size_t i = 0; i < board.size(); ++i) { CHECK(board[i].slot_id == expected[i]); }
    CHECK(next_departures({{1, at}}, 4).size() == 1);
    CHECK(next_departures({}, 4).empty());
}
