#include "doctest.h"
#include "tee_rules.h"
#include "game/server_errors.h"
#include "game/course_world_loader.h"
#include "game/json_util.h"
#include "test_support.h"

TEST_CASE("tee times: UTC quarters, full group, atomic replacement guards and stale booking ids") {
    const auto& tuning = shipped_content().tuning.server;
    const auto at = 100 * 15 * minute_micros;
    CHECK(next_tee_time(at - 1, tuning) == at);
    CHECK(next_tee_time(at, tuning) == at);
    CHECK(next_tee_time(at + 1, tuning) == at + 15 * minute_micros);
    CHECK(tuning.group_capacity == 5); // named owner departure, not real-golf foursomes
    CHECK(check_booking(7, 7, at, at - 1, 4, false, tuning).empty());
    CHECK(check_booking(7, 7, at, at - 1, 5, false, tuning) == error_group_full);
    CHECK(check_booking(7, 7, at, at - 1, 5, true, tuning).empty());
    CHECK(check_booking(6, 7, at, at - 1, 0, false, tuning) == error_stale_booking);
    CHECK(check_booking(0, 7, at, at - 1, 0, false, tuning) == error_stale_booking);
    CHECK(check_booking(7, 7, at + 1, at - 1, 0, false, tuning) == error_invalid_slot);
    CHECK(check_booking(7, 7, at, at + 1, 0, false, tuning) == error_invalid_slot);
    CHECK(check_booking(7, 7, at + 2 * 24 * 60 * minute_micros, at - 1, 0, false, tuning) == error_invalid_slot);

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
    const auto world = parse_world_from_text(root->dump());
    REQUIRE(world);
    REQUIRE(world->hub);
    CHECK(world->hub->book.reach == 2);
    REQUIRE(world->bus_stop);
    CHECK(world->bus_stop->position.x == 6);
    CHECK(world->bus_stop->heading == 1.5f);
    CHECK(!parse_course_world_from_text(root->dump(), course));
    root->erase("hub");
    CHECK(parse_course_world_from_text(root->dump(), course));
    (*root)["bus_stop"].erase("heading");
    CHECK(!parse_course_world_from_text(root->dump(), course));
}
