#include "doctest.h"

#include "game/game_state.h"
#include "game/knockout.h"
#include "game/server_errors.h"
#include "game/lightning_play.h"
#include "game/lightning_rules.h"
#include "game/mode_dispatch.h"
#include "game/weather.h"

#include "lightning_check_rules.h"
#include "server_test_support.h"
#include "test_support.h"

#include <cmath>
#include <optional>
#include <vector>

namespace {
const game_tuning& tuning() {
    return shipped_content().tuning;
}

const lightning_tuning& numbers() {
    return tuning().lightning;
}

constexpr float open_rate_at_full_thunder = 1.0f / 86400.0f;

solid canopy_at(const glm::vec3& base, const float radius) {
    solid leaves;
    leaves.shape = solid_shape::cone;
    leaves.kind = solid_kind::leaves;
    leaves.base = base;
    leaves.radius = radius;
    leaves.height = 5.0f;
    leaves.porous = true;
    return leaves;
}

// A building's roof: a board box whose underside is `underside` over the ground.
solid roof_at(const glm::vec3& centre, const float underside) {
    solid roof;
    roof.shape = solid_shape::box;
    roof.kind = solid_kind::board;
    roof.base = glm::vec3(centre.x, underside, centre.z);
    roof.height = 0.3f;
    roof.along = glm::vec3(0.0f, 0.0f, 1.0f);
    roof.half_length = 4.0f;
    roof.half_width = 4.0f;
    return roof;
}

lightning_cover cover_at(const std::vector<solid>& solids, const glm::vec3& feet) {
    return cover_over(solids, build_solid_grid(solids, 16.0f), feet, tuning());
}

int strikes_over(const int golfers, const int checks, const float thunder, const game_tuning& rules, const std::uint64_t seed,
                 int* knockouts = nullptr) {
    std::vector<lightning_golfer> field;
    for (int i = 0; i < golfers; ++i) {
        field.push_back(lightning_golfer{static_cast<std::uint64_t>(i + 1), glm::vec3(0.0f), false});
    }
    const std::vector<solid> none;
    const solid_grid grid = build_solid_grid(none, 16.0f);
    int total = 0;
    for (int check = 0; check < checks; ++check) {
        for (const lightning_strike_hit& hit :
             lightning_strikes(field, thunder, lightning_check_seed(seed, check), none, grid, rules)) {
            ++total;
            if (knockouts != nullptr && hit.outcome == lightning_outcome::knocked_out) {
                ++*knockouts;
            }
        }
    }
    return total;
}
}

TEST_CASE("in the open at full thunder a golfer is struck about once in 24 storm-hours") {
    CHECK(numbers().open_strike_hours == 24.0f);
    const float rate = strike_rate_per_second(1.0f, lightning_cover::open, false, numbers());
    CHECK(std::abs(rate - open_rate_at_full_thunder) < 1e-9f);
    CHECK(std::abs(rate * 86400.0f - 1.0f) < 1e-4f);
    // A ten-second check's chance is the rate over ten seconds, however small.
    CHECK(std::abs(strike_chance(rate, numbers().check_seconds) - rate * numbers().check_seconds) < 1e-8f);
    CHECK(strike_chance(0.0f, 10.0f) == 0.0f);
    CHECK(strike_chance(100.0f, 10.0f) > 0.999f);
}

TEST_CASE("over many golfers and a storm-day of checks the strikes come at the stated rate") {
    constexpr int golfers = 200;
    const int checks = static_cast<int>(86400.0f / numbers().check_seconds);
    // One expected each: Poisson with a mean of 200, 3 or 4 deviations either way.
    for (const std::uint64_t seed : {1ull, 2ull}) {
        const int struck = strikes_over(golfers, checks, 1.0f, tuning(), seed);
        CHECK(struck > 140);
        CHECK(struck < 265);
    }
}

TEST_CASE("nobody is struck at or below the thunderstorm level, and the storm factor rises evenly to full thunder") {
    CHECK(storm_factor(0.0f) == 0.0f);
    CHECK(storm_factor(thunderstorm_thunder) == 0.0f);
    CHECK(storm_factor(thunderstorm_thunder - 0.01f) == 0.0f);
    CHECK(std::abs(storm_factor(0.75f) - 0.5f) < 1e-6f);
    CHECK(storm_factor(1.0f) == 1.0f);
    CHECK(storm_factor(2.0f) == 1.0f);
    game_tuning every = tuning();
    every.lightning.open_strike_hours = 1e-9f;  // a strike every check, wherever it can come
    CHECK(strikes_over(20, 20, thunderstorm_thunder, every, 3) == 0);
    CHECK(strikes_over(20, 20, 0.2f, every, 3) == 0);
    CHECK(strikes_over(20, 20, 0.6f, every, 3) == 20 * 20);
    CHECK(strike_rate_per_second(thunderstorm_thunder, lightning_cover::open, true, numbers()) == 0.0f);
}

TEST_CASE("a building's roof shelters, a tree's canopy and a club in hand make it worse, a cart is no shelter") {
    const float open = strike_rate_per_second(1.0f, lightning_cover::open, false, numbers());
    const float roofed = strike_rate_per_second(1.0f, lightning_cover::building, false, numbers());
    const float treed = strike_rate_per_second(1.0f, lightning_cover::tree, false, numbers());
    const float at_ball = strike_rate_per_second(1.0f, lightning_cover::open, true, numbers());
    CHECK(roofed < 0.05f * open);
    CHECK(roofed > 0.0f);
    CHECK(treed > open);
    CHECK(at_ball > open);
    CHECK(strike_rate_per_second(1.0f, lightning_cover::tree, true, numbers()) > treed);

    const glm::vec3 feet(0.0f);
    solid cart;
    cart.shape = solid_shape::box;
    cart.kind = solid_kind::cart;
    cart.base = feet;
    cart.height = 1.8f;
    cart.along = glm::vec3(0.0f, 0.0f, 1.0f);
    cart.half_length = 1.5f;
    cart.half_width = 0.6f;
    CHECK(cover_at({cart}, feet) == lightning_cover::open);
    CHECK(strike_rate_per_second(1.0f, cover_at({cart}, feet), false, numbers()) == open);
}

TEST_CASE("what is over a golfer's head decides their cover: the nearest canopy or roof, never what is beside them") {
    const float head = tuning().rangefinder.person_height;
    const glm::vec3 under_tree(1.0f, 0.0f, 0.0f);
    const std::vector<solid> tree{canopy_at(glm::vec3(0.0f, head + 0.7f, 0.0f), 4.0f)};
    CHECK(cover_at(tree, under_tree) == lightning_cover::tree);
    CHECK(cover_at(tree, glm::vec3(6.0f, 0.0f, 0.0f)) == lightning_cover::open);
    // A canopy standing on the ground (a bush) is not over a head.
    CHECK(cover_at({canopy_at(glm::vec3(0.0f), 4.0f)}, under_tree) == lightning_cover::open);
    // A canopy lower than the head is no cover either.
    CHECK(cover_at({canopy_at(glm::vec3(0.0f, head - 0.2f, 0.0f), 4.0f)}, under_tree) == lightning_cover::open);

    const std::vector<solid> shelter{roof_at(glm::vec3(10.0f, 0.0f, 0.0f), head + 1.0f)};
    CHECK(cover_at(shelter, glm::vec3(10.5f, 0.0f, -2.0f)) == lightning_cover::building);
    CHECK(cover_at(shelter, glm::vec3(15.0f, 0.0f, 0.0f)) == lightning_cover::open);
    // The ground the golfer stands at counts: a roof over a rise is not over a golfer below.
    CHECK(cover_at(shelter, glm::vec3(10.5f, 3.0f, 0.0f)) == lightning_cover::open);

    // A building's body standing on the ground, beside them, shelters nobody.
    solid body = roof_at(glm::vec3(20.0f, 0.0f, 0.0f), 0.0f);
    body.height = 6.0f;
    CHECK(cover_at({body}, glm::vec3(26.0f, 0.0f, 0.0f)) == lightning_cover::open);

    // Both overhead: the lower underside is what is nearest.
    const std::vector<solid> both{roof_at(glm::vec3(0.0f), head + 2.0f), canopy_at(glm::vec3(0.0f, head + 0.5f, 0.0f), 4.0f)};
    CHECK(cover_at(both, glm::vec3(0.0f)) == lightning_cover::tree);
    const std::vector<solid> roof_lower{roof_at(glm::vec3(0.0f), head + 0.5f), canopy_at(glm::vec3(0.0f, head + 2.0f, 0.0f), 4.0f)};
    CHECK(cover_at(roof_lower, glm::vec3(0.0f)) == lightning_cover::building);
}

TEST_CASE("the rule's cover reaches the strikes: a roof shelters, a canopy and a club in hand strike more") {
    game_tuning every = tuning();
    every.lightning.open_strike_hours = 0.05f;  // a strike every twenty checks in the open
    const float head = every.rangefinder.person_height;
    const std::vector<solid> world{roof_at(glm::vec3(50.0f, 0.0f, 0.0f), head + 1.0f),
                                   canopy_at(glm::vec3(0.0f, head + 0.7f, 0.0f), 4.0f)};
    const solid_grid grid = build_solid_grid(world, 16.0f);
    const auto struck = [&](const glm::vec3& at, const bool club) {
        int total = 0;
        for (int check = 0; check < 3000; ++check) {
            const std::vector<lightning_golfer> field{lightning_golfer{9, at, club}};
            total += static_cast<int>(lightning_strikes(field, 1.0f, lightning_check_seed(5, check), world, grid, every).size());
        }
        return total;
    };
    const int open = struck(glm::vec3(30.0f, 0.0f, 0.0f), false);
    CHECK(open > 0);
    CHECK(struck(glm::vec3(50.0f, 0.0f, 0.0f), false) < open / 4);
    CHECK(struck(glm::vec3(0.0f, 0.0f, 0.0f), false) > open);
    CHECK(struck(glm::vec3(30.0f, 0.0f, 0.0f), true) > open);
}

TEST_CASE("one strike in ten knocks out, the rest daze") {
    CHECK(numbers().knockout_share == 0.1f);
    game_tuning often = tuning();
    often.lightning.open_strike_hours = 0.05f;  // frequent enough to count
    int knockouts = 0;
    const int struck = strikes_over(1000, 200, 1.0f, often, 11, &knockouts);
    REQUIRE(struck > 8000);
    const float share = static_cast<float>(knockouts) / static_cast<float>(struck);
    CHECK(share > 0.09f);
    CHECK(share < 0.11f);
}

TEST_CASE("the same seed strikes the same golfers the same way, another seed does not") {
    game_tuning often = tuning();
    often.lightning.open_strike_hours = 0.05f;
    std::vector<lightning_golfer> field;
    for (std::uint64_t account = 1; account <= 300; ++account) {
        field.push_back(lightning_golfer{account, glm::vec3(static_cast<float>(account), 0.0f, 0.0f), account % 3 == 0});
    }
    const std::vector<solid> none;
    const solid_grid grid = build_solid_grid(none, 16.0f);
    const auto roll = [&](const std::uint64_t seed) {
        return lightning_strikes(field, 0.9f, lightning_check_seed(seed, 77), none, grid, often);
    };
    const std::vector<lightning_strike_hit> first = roll(1);
    const std::vector<lightning_strike_hit> again = roll(1);
    const std::vector<lightning_strike_hit> other = roll(2);
    REQUIRE(!first.empty());
    REQUIRE(first.size() == again.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].account == again[i].account);
        CHECK(first[i].outcome == again[i].outcome);
        CHECK(first[i].position == again[i].position);
    }
    bool differs = first.size() != other.size();
    for (std::size_t i = 0; !differs && i < first.size(); ++i) {
        differs = first[i].account != other[i].account || first[i].outcome != other[i].outcome;
    }
    CHECK(differs);
    // A check is a whole span of the shared clock, the same for everyone in it.
    const std::int64_t base = 1'800'000'000'000;
    const std::int64_t length = static_cast<std::int64_t>(numbers().check_seconds * 1000.0f);
    CHECK(lightning_check_at(base, numbers()) == lightning_check_at(base + length - 1 - base % length, numbers()));
    CHECK(lightning_check_at(base - base % length + length, numbers()) == lightning_check_at(base, numbers()) + 1);
}

namespace {
// The time in a year, on the hour, when `course` thunders most.
std::int64_t stormiest_time(const server_content& content, const server_course& course) {
    std::int64_t best_at = 0;
    float best = -1.0f;
    for (std::int64_t hour = 0; hour < 24 * 365; ++hour) {
        const std::int64_t at = 1'800'000'000'000 + hour * 3'600'000;
        const float thunder =
            weather_at(content.tuning.weather, content.tuning.wind, content.tuning.sky_clock, course.course, at).thunder;
        if (thunder > best) {
            best = thunder;
            best_at = at;
        }
    }
    return best_at;
}

// Something calm: no thunder at all.
std::int64_t calm_time(const server_content& content, const server_course& course) {
    for (std::int64_t hour = 0; hour < 24 * 30; ++hour) {
        const std::int64_t at = 1'800'000'000'000 + hour * 3'600'000;
        if (weather_at(content.tuning.weather, content.tuning.wind, content.tuning.sky_clock, course.course, at).thunder <= 0.0f) {
            return at;
        }
    }
    return 0;
}
}

TEST_CASE("the server's check rolls a room in a thunderstorm and passes a calm one over without looking at who is in it") {
    const content_fixture fixture = fixture_content_files();
    server_content content = *parse_server_content(fixture.files).content;
    const std::optional<server_course> course = build_server_course(fixture.files, content, fixture_hub_course().id);
    REQUIRE(course.has_value());
    const std::int64_t storm = stormiest_time(content, *course);
    const float thunder =
        weather_at(content.tuning.weather, content.tuning.wind, content.tuning.sky_clock, course->course, storm).thunder;
    REQUIRE(thunder > thunderstorm_thunder);
    const std::int64_t calm = calm_time(content, *course);
    REQUIRE(calm != 0);

    content.tuning.lightning.open_strike_hours = 1e-9f;  // whoever can be struck is
    content.tuning.lightning.knockout_share = 1.0f;
    int lookups = 0;
    const auto golfers_in = [&lookups](const std::uint64_t room) {
        ++lookups;
        return std::vector<lightning_golfer>{lightning_golfer{room * 10 + 1, glm::vec3(1.0f, 2.0f, 3.0f), false},
                                             lightning_golfer{room * 10 + 2, glm::vec3(4.0f, 5.0f, 6.0f), true}};
    };
    const std::vector<lightning_room_ref> rooms{lightning_room_ref{3, &*course}, lightning_room_ref{4, nullptr}};

    CHECK(check_lightning_rooms(rooms, golfers_in, content, calm, 99).empty());
    CHECK(lookups == 0);

    const std::vector<room_lightning_hit> hits = check_lightning_rooms(rooms, golfers_in, content, storm, 99);
    CHECK(lookups == 1);  // the room with no known course is passed over
    REQUIRE(hits.size() == 2U);
    CHECK(hits[0].room_id == 3U);
    CHECK(hits[0].hit.account == 31U);
    CHECK(hits[0].hit.position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(hits[0].hit.outcome == lightning_outcome::knocked_out);
    CHECK(hits[1].hit.account == 32U);

    content.tuning.lightning.knockout_share = 0.0f;
    for (const room_lightning_hit& each : check_lightning_rooms(rooms, golfers_in, content, storm, 99)) {
        CHECK(each.hit.outcome == lightning_outcome::dazed);
    }
}

TEST_CASE("the server's secret decides who is struck: another secret is another check") {
    const content_fixture fixture = fixture_content_files();
    server_content content = *parse_server_content(fixture.files).content;
    const std::optional<server_course> course = build_server_course(fixture.files, content, fixture_hub_course().id);
    REQUIRE(course.has_value());
    const std::int64_t storm = stormiest_time(content, *course);
    content.tuning.lightning.open_strike_hours = 0.001f;
    const auto crowd = [](const std::uint64_t) {
        std::vector<lightning_golfer> golfers;
        for (std::uint64_t account = 1; account <= 200; ++account) {
            golfers.push_back(lightning_golfer{account, glm::vec3(0.0f), false});
        }
        return golfers;
    };
    const std::vector<lightning_room_ref> rooms{lightning_room_ref{3, &*course}};
    const std::vector<room_lightning_hit> first = check_lightning_rooms(rooms, crowd, content, storm, 1);
    const std::vector<room_lightning_hit> again = check_lightning_rooms(rooms, crowd, content, storm, 1);
    const std::vector<room_lightning_hit> other = check_lightning_rooms(rooms, crowd, content, storm, 2);
    REQUIRE(!first.empty());
    REQUIRE(first.size() == again.size());
    bool differs = first.size() != other.size();
    for (std::size_t i = 0; i < first.size(); ++i) {
        CHECK(first[i].hit.account == again[i].hit.account);
        differs = differs || (i < other.size() && first[i].hit.account != other[i].hit.account);
    }
    CHECK(differs);
}

namespace {
game_state stormy_offline_game(const float knockout_share) {
    game_state state = started_hole();
    state.tuning.lightning.open_strike_hours = 1e-9f;  // struck at the first check
    state.tuning.lightning.knockout_share = knockout_share;
    state.weather_override = with_weather_kind(state.weather, weather_kind::thunderstorm, state.tuning.weather);
    state.local_unix_ms = 1'800'000'000'000;
    return state;
}

void play_through_a_check(game_state& state) {
    update_game(state, game_input{}, 0.016f);
    state.local_unix_ms += static_cast<std::int64_t>(state.tuning.lightning.check_seconds * 1000.0f) + 1;
    update_game(state, game_input{}, 0.016f);
}
}

TEST_CASE("offline, a check in a thunderstorm can strike me: a daze, or a knockout that lays my body and sends me to the menu") {
    game_state dazed = stormy_offline_game(0.0f);
    update_game(dazed, game_input{}, 0.016f);
    CHECK(dazed.strike_points.empty());  // the check under way is not rolled
    play_through_a_check(dazed);
    REQUIRE(dazed.strike_points.size() >= 1U);
    CHECK(dazed.strike_points.front() == dazed.player.position);
    CHECK(!dazed.knocked_out);
    CHECK(event_started_at(running_effects(dazed.effects, shot_unix_ms(dazed)), "dazed", shot_unix_ms(dazed)).has_value());

    game_state out = stormy_offline_game(1.0f);
    play_through_a_check(out);
    CHECK(out.knocked_out);
    CHECK(out.save_requested);
    REQUIRE(out.save.bodies.size() == 1U);
    CHECK(out.save.bodies.front().place_id == out.place.id);
    CHECK(out.strike_points.size() >= 1U);
}

TEST_CASE("offline, a clear sky or a calm check strikes nobody") {
    game_state calm = stormy_offline_game(1.0f);
    calm.weather_override = with_weather_kind(calm.weather, weather_kind::clear, calm.tuning.weather);
    play_through_a_check(calm);
    CHECK(calm.strike_points.empty());
    CHECK(!calm.knocked_out);
    game_state storm = stormy_offline_game(1.0f);
    storm.tuning.lightning.open_strike_hours = 24.0f;
    for (int i = 0; i < 3; ++i) {
        update_game(storm, game_input{}, 0.016f);
        storm.local_unix_ms += 1;  // inside one check: nothing more to roll
    }
    CHECK(storm.strike_points.empty());
}

TEST_CASE("a god strike is the same hit a check gives, and the server refuses it unless it allows god commands") {
    const room_lightning_hit dazed = god_strike_hit(3, 31, glm::vec3(1.0f, 2.0f, 3.0f), false);
    CHECK(dazed.room_id == 3U);
    CHECK(dazed.hit.account == 31U);
    CHECK(dazed.hit.position == glm::vec3(1.0f, 2.0f, 3.0f));
    CHECK(dazed.hit.outcome == lightning_outcome::dazed);
    CHECK(god_strike_hit(3, 31, glm::vec3(0.0f), true).hit.outcome == lightning_outcome::knocked_out);

    CHECK(god_command_refusal(false) == error_god_disabled);
    CHECK(god_command_refusal(true).empty());
}
