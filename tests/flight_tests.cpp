#include "doctest.h"

#include "game/content_files.h"
#include "game/json_util.h"
#include "game/weather.h"
#include "physics/ball_physics.h"
#include "physics/flight_model.h"
#include "physics/launch.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
// How a ball flew from level ground until it came back down to it.
struct flight {
    float carry = 0.0f;
    float apex = 0.0f;
    float land_angle_degrees = 0.0f;
};

// TrackMan's launch numbers in still `air`, stepped `dt` at a time with the
// shipped ball.
flight fly(const json& club, const air_state& air, const float dt = 1.0f / 120.0f) {
    const physics_tuning& physics = shipped_content().tuning.physics;
    launch_conditions launch;
    launch.ball_speed = *json_float(club, "ball_speed");
    launch.launch_angle = glm::radians(*json_float(club, "launch"));
    launch.spin = *json_float(club, "spin") * glm::two_pi<float>() / 60.0f;
    ball_state ball = launched_ball(glm::vec3(0.0f), shipped_content().tuning.scale.ball_physics_radius_meters, launch);
    flight out;
    for (int step = 0; step < 120 * 30; ++step) {
        const ball_state next = step_ball_flight(ball, wind_state{}, air, false, dt, physics);
        out.apex = std::max(out.apex, next.position.y);
        if (next.position.y < 0.0f) {
            const float share = ball.position.y / (ball.position.y - next.position.y);
            out.carry = ball.position.z + (next.position.z - ball.position.z) * share;
            out.land_angle_degrees = glm::degrees(std::atan2(-next.velocity.y, next.velocity.z));
            return out;
        }
        ball = next;
    }
    return out;
}

json tour_clubs() {
    return *json_array(*parse_json(*read_text_file(fixture_root() + "/reference/trackman_tour.json")), "clubs");
}

air_state sea_level(const float temperature_c = 15.0f) {
    return air_at(shipped_content().tuning.physics.air, 0.0f, temperature_c);
}
}

TEST_CASE("the air is the standard atmosphere's, thinner up high and when warm") {
    const air_tuning& air = shipped_content().tuning.physics.air;
    CHECK(near(air_at(air, 0.0f, 15.0f).density, 1.225f, 0.002f));
    // 1000 m up at the same temperature: the pressure's fall alone.
    CHECK(near(air_at(air, 1000.0f, 15.0f).density, 1.0866f, 0.002f));
    // About 1.7% denser per 5 C colder (tests/fixtures/reference/aerodynamics.json).
    const float colder = air_at(air, 0.0f, 10.0f).density / air_at(air, 0.0f, 15.0f).density - 1.0f;
    CHECK(colder > 0.016f);
    CHECK(colder < 0.019f);
}

TEST_CASE("TrackMan's tour launches carry as far as TrackMan measured, within 3% (the driver 4%)") {
    const json clubs = tour_clubs();
    REQUIRE(clubs.size() == 12U);
    const json acceptance = (*parse_json(*read_text_file(fixture_root() + "/reference/aerodynamics.json")))["acceptance"];
    for (const json& club : clubs) {
        const flight flown = fly(club, sea_level());
        const float measured = *json_float(club, "carry");
        const float error = flown.carry / measured - 1.0f;
        const bool driver = *json_string(club, "club") == "driver";
        const float allowed = *json_float(acceptance, driver ? "driver_carry_share" : "carry_share");
        if (std::abs(error) > allowed) {
            std::printf("%s: carry %.1f against %.1f\n", json_string(club, "club")->c_str(), flown.carry, measured);
        }
        CHECK(std::abs(error) <= allowed);
        // And they peak and come down roughly as measured.
        CHECK(std::abs(flown.apex - *json_float(club, "max_height")) < 6.0f);
        CHECK(std::abs(flown.land_angle_degrees - *json_float(club, "land_angle")) < 9.0f);
    }
}

TEST_CASE("a drive's carry hardly moves with the step: midpoint steps at 120 Hz are fine enough") {
    const json driver = tour_clubs().front();
    const float at_120 = fly(driver, sea_level(), 1.0f / 120.0f).carry;
    const float at_240 = fly(driver, sea_level(), 1.0f / 240.0f).carry;
    CHECK(std::abs(at_120 - at_240) < 0.3f);
}

TEST_CASE("a cool morning drive carries a few metres shorter than a warm afternoon one") {
    const json driver = tour_clubs().front();
    const json reference = *parse_json(*read_text_file(fixture_root() + "/reference/aerodynamics.json"));
    const json& per_10 = reference["temperature"]["drive_carry_per_10_c"];
    const float low = per_10["range"][0].get<float>();
    const float high = per_10["range"][1].get<float>();
    const float gain = (fly(driver, sea_level(25.0f)).carry - fly(driver, sea_level(5.0f)).carry) / 2.0f;
    CHECK(gain > low);
    CHECK(gain < high);
    // Thin mountain air carries it further still.
    const float mountain = fly(driver, air_at(shipped_content().tuning.physics.air, 1600.0f, 15.0f)).carry;
    CHECK(mountain > fly(driver, sea_level()).carry * 1.04f);
    CHECK(mountain < fly(driver, sea_level()).carry * 1.09f);
}

TEST_CASE("a course's air warms through the day: coldest at dawn, warmest mid-afternoon, less so under cloud") {
    const game_tuning& tuning = shipped_content().tuning;
    const course_definition& course = shipped_content().courses.front();
    // `hour` local: the game's hours are UTC.
    const auto at = [&](const float hour, const float cloud, const float rain) {
        return temperature_at(tuning.weather, course, static_cast<double>(hour - course.sky.utc_offset_hours) + 24.0, cloud, rain, 0.5f);
    };
    const float dawn = at(tuning.weather.temperature.coldest_hour, 0.0f, 0.0f);
    const float afternoon = at(tuning.weather.temperature.warmest_hour, 0.0f, 0.0f);
    CHECK(near(afternoon - dawn, course.air.daily_temperature_range_c, 0.01f));
    CHECK(near((afternoon + dawn) * 0.5f, course.air.mean_temperature_c, 0.01f));
    CHECK(at(9.0f, 0.0f, 0.0f) > dawn);
    CHECK(at(9.0f, 0.0f, 0.0f) < afternoon);
    CHECK(at(21.0f, 0.0f, 0.0f) < afternoon);
    CHECK(at(21.0f, 0.0f, 0.0f) > dawn);
    // Overcast flattens the day; rain cools it.
    CHECK(at(15.0f, 1.0f, 0.0f) - at(5.0f, 1.0f, 0.0f) < afternoon - dawn);
    CHECK(at(15.0f, 0.0f, 1.0f) < afternoon);
    // A whole day round, it comes back to where it started.
    CHECK(near(at(0.0f, 0.0f, 0.0f), at(24.0f, 0.0f, 0.0f), 0.0001f));
}
