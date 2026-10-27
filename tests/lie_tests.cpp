#include "doctest.h"

#include "game/delivery.h"
#include "game/shot_simulation.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
const club_definition& club_labelled(const std::string& label) {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    return *std::find_if(clubs.begin(), clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

launch_conditions struck_from(const std::string& label, const ball_on_lie& on) {
    return club_strike(club_labelled(label), stock_swing(club_labelled(label), shipped_content().tuning, 1.0f, (on).lie, 0.0f), on, shipped_content().tuning).launch;
}

ball_on_lie on_stance(const float ball_above_feet_degrees, const float uphill_degrees) {
    return ball_on_lie{ball_lie::fairway,
                       stance_slope{glm::radians(ball_above_feet_degrees), glm::radians(uphill_degrees)}, 0.0f};
}
}

TEST_CASE("a ball above the feet turns the face left by atan(tan(loft) sin(slope)), more with more loft, and draws") {
    const game_tuning& tuning = shipped_content().tuning;
    const float tilt = glm::radians(10.0f);
    float turned_before = 0.0f;
    for (const char* label : {"5I", "9I"}) {
        const club_definition& club = club_labelled(label);
        const delivery stock = club_delivery(club, setup_dialled(club, 0.0f, 1.0f, 0.0f, club.spec.stock_ball_position), tuning.swing);
        const delivery above = lie_delivery(stock, club, on_stance(10.0f, 0.0f), 1.0f,
                                            tuning.scale.ball_physics_radius_meters, tuning.lies);
        const float turned = above.face - stock.face;
        CHECK(near(turned, std::atan(std::tan(stock.dynamic_loft) * std::sin(tilt)), 0.0005f));
        CHECK(turned > turned_before);
        turned_before = turned;
    }
    // Starts left and curves left for a right-hander; below the feet, right.
    const launch_conditions above = struck_from("9I", on_stance(10.0f, 0.0f));
    const launch_conditions below = struck_from("9I", on_stance(-10.0f, 0.0f));
    CHECK(above.direction > 0.0f);
    CHECK(above.spin_axis_tilt > 0.0f);
    CHECK(below.direction < 0.0f);
    CHECK(below.spin_axis_tilt < 0.0f);
}

TEST_CASE("an uphill stance launches the ball higher and a downhill one lower, with the same spin") {
    const launch_conditions flat = struck_from("7I", on_stance(0.0f, 0.0f));
    const launch_conditions up = struck_from("7I", on_stance(0.0f, 5.0f));
    const launch_conditions down = struck_from("7I", on_stance(0.0f, -5.0f));
    CHECK(up.launch_angle > flat.launch_angle + glm::radians(4.0f));
    CHECK(down.launch_angle < flat.launch_angle - glm::radians(4.0f));
    CHECK(near(up.spin, flat.spin, flat.spin * 0.001f));
    CHECK(near(down.ball_speed, flat.ball_speed, flat.ball_speed * 0.001f));
}

TEST_CASE("a stance's slope is the ground under the feet against the ground under the ball") {
    const game_tuning& tuning = shipped_content().tuning;
    // Rising one in ten towards the pin.
    const play_area area = hole_area(straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 12.0f, 120.0f), 30.0f), tuning);
    const glm::vec3 ball = resting_ball_position(area, glm::vec3(0.0f, 0.0f, 60.0f), tuning.scale.ball_physics_radius_meters);
    REQUIRE(lie_at(area, ball) != ball_lie::tee);
    const float grade = std::atan(0.1f);
    const float tolerance = glm::radians(1.0f);

    const stance_slope towards = stance_at(area, ball, 0.0f, handedness::right, tuning);
    CHECK(near(towards.uphill, grade, tolerance));
    CHECK(near(towards.ball_above_feet, 0.0f, tolerance));
    const stance_slope away = stance_at(area, ball, glm::pi<float>(), handedness::right, tuning);
    CHECK(near(away.uphill, -grade, tolerance));
    // Aimed across the slope, a right-hander stands on the ball's left.
    const stance_slope across_right = stance_at(area, ball, glm::radians(90.0f), handedness::right, tuning);  // feet down the slope
    CHECK(near(across_right.ball_above_feet, grade, tolerance));
    CHECK(near(across_right.uphill, 0.0f, tolerance));
    const stance_slope across_left = stance_at(area, ball, glm::radians(-90.0f), handedness::right, tuning);
    CHECK(near(across_left.ball_above_feet, -grade, tolerance));
}

TEST_CASE("a driver off the ground is struck low and spins more than off a tee, while an iron meets the ball alike") {
    const ball_on_lie teed{ball_lie::tee, {}, 0.0f};
    const ball_on_lie deck{ball_lie::fairway, {}, 0.0f};
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& driver = club_labelled("DR");
    const strike_outcome off_tee = club_strike(driver, stock_swing(driver, tuning, 1.0f, (teed).lie, 0.0f), teed, tuning);
    const strike_outcome off_deck = club_strike(driver, stock_swing(driver, tuning, 1.0f, (deck).lie, 0.0f), deck, tuning);
    CHECK(off_deck.launch.spin > off_tee.launch.spin * 1.1f);
    CHECK(off_deck.launch.ball_speed < off_tee.launch.ball_speed);
    CHECK(off_deck.centredness < off_tee.centredness);

    const launch_conditions iron_tee = struck_from("7I", teed);
    const launch_conditions iron_deck = struck_from("7I", deck);
    CHECK(iron_deck.ball_speed == iron_tee.ball_speed);
    CHECK(iron_deck.spin == iron_tee.spin);
}
