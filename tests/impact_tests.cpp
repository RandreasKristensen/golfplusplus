#include "doctest.h"

#include "game/content_files.h"
#include "game/delivery.h"
#include "game/json_util.h"
#include "game/shot_simulation.h"
#include "physics/ball_physics.h"
#include "physics/flight_model.h"
#include "physics/impact.h"
#include "physics/launch.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include <glm/trigonometric.hpp>

namespace {
const club_definition& shipped_club(const std::string& label) {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    return *std::find_if(clubs.begin(), clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

struct flight {
    float carry = 0.0f;
    float apex = 0.0f;
    glm::vec3 landing{0.0f};
};

// A ball leaving level ground as `launch` says, in still air at sea level
// and 15 C, until it comes back down to the ground.
flight fly(const launch_conditions& launch) {
    const game_tuning& tuning = shipped_content().tuning;
    const air_state air = air_at(tuning.physics.air, 0.0f, 15.0f);
    ball_state ball = launched_ball(glm::vec3(0.0f), tuning.scale.ball_physics_radius_meters, launch);
    flight out;
    for (int step = 0; step < 120 * 30; ++step) {
        const ball_state next = step_ball_flight(ball, wind_state{}, air, false, 1.0f / 120.0f, tuning.physics);
        out.apex = std::max(out.apex, next.position.y);
        if (next.position.y < 0.0f) {
            const float share = ball.position.y / (ball.position.y - next.position.y);
            out.landing = ball.position + (next.position - ball.position) * share;
            out.carry = horizontal_distance(out.landing, glm::vec3(0.0f));
            return out;
        }
        ball = next;
    }
    return out;
}

launch_conditions struck(const club_definition& club, const delivery& swing) {
    const game_tuning& tuning = shipped_content().tuning;
    return impact(club.spec, swing, lie_contact{}, tuning.physics.aero.mass, tuning.scale.ball_physics_radius_meters).launch;
}

delivery stock(const club_definition& club) {
    return club_delivery(club, setup_dialled(club, 0.0f, 1.0f, 0.0f, club.spec.stock_ball_position), shipped_content().tuning.swing);
}
}

TEST_CASE("the bag at 99 carries what the bag table says, within its tolerance, each club shorter than the last") {
    const json root = *parse_json(*read_text_file(fixture_root() + "/reference/bag_targets.json"));
    const float tolerance = *json_float(root, "tolerance_share");
    float previous = 1.0e9f;
    int checked = 0;
    for (const club_definition& club : shipped_content().clubs) {
        if (club.club_class == "putter") {
            continue;
        }
        const json* target = nullptr;
        for (const json& row : *json_array(root, "clubs")) {
            if (json_string(row, "label") == club.label) {
                target = &row;
            }
        }
        REQUIRE(target != nullptr);
        const float expected = *json_float(*target, "carry");
        CHECK(json_float(*target, "static_loft") == club.spec.static_loft_degrees);
        const float carry = fly(struck(club, stock(club))).carry;
        CHECK(std::abs(carry - expected) <= tolerance * expected);
        CHECK(carry < previous);
        previous = carry;
        ++checked;
    }
    CHECK(checked == 13);
}

TEST_CASE("the ball starts mostly where the face points and curves away from the path, by the D-plane") {
    const club_definition& seven = shipped_club("7I");
    const float path = glm::radians(10.0f);
    for (const float face_to_path_degrees : {-4.0f, 4.0f}) {
        delivery swing = stock(seven);
        swing.path = path;
        swing.face = path + glm::radians(face_to_path_degrees);
        const launch_conditions launch = struck(seven, swing);
        // Start: between path and face, most of the way to the face for an iron.
        const float start_share = (launch.direction - path) / (swing.face - path);
        CHECK(start_share > 0.6f);
        CHECK(start_share < 0.9f);
        // Curve: further the face's way of the path, measured from the start line.
        const flight landed = fly(launch);
        const float turned = wrap_angle(yaw_towards(glm::vec3(0.0f), landed.landing) - launch.direction);
        CHECK(turned * face_to_path_degrees > 0.0f);
        CHECK(std::abs(glm::degrees(turned)) > 1.0f);
    }
    // Face square to the path: no curve.
    delivery square = stock(seven);
    square.path = path;
    square.face = path;
    const launch_conditions straight = struck(seven, square);
    CHECK(near(straight.direction, path, 1e-5f));
    CHECK(near(wrap_angle(yaw_towards(glm::vec3(0.0f), fly(straight).landing) - path), 0.0f, 1e-3f));
}

TEST_CASE("an open face lies the loft back: higher, spinnier and shorter") {
    CHECK(face_open_loft_gain(0.0f, 64.0f) == 0.0f);
    // Opened about a shaft leaning 64 degrees from level, a face gains about
    // a third of the turn as loft (asin(sin 20 cos 64) = 8.6 degrees).
    CHECK(near(glm::degrees(face_open_loft_gain(glm::radians(20.0f), 64.0f)), 8.6f, 0.1f));

    const club_definition& wedge = shipped_club("SW");
    const swing_tuning& tuning = shipped_content().tuning.swing;
    const delivery square = club_delivery(wedge, setup_dialled(wedge, 0.0f, 1.0f, 0.0f, 0.0f), tuning);
    const delivery open = club_delivery(wedge, setup_dialled(wedge, 0.0f, 1.0f, glm::radians(20.0f), 0.0f), tuning);
    CHECK(open.dynamic_loft > square.dynamic_loft);
    // Aimed left to start it at the target: the path left of the open face.
    delivery played = open;
    played.path = glm::radians(10.0f);
    played.face = played.path - glm::radians(20.0f);
    const launch_conditions flop = struck(wedge, played);
    const launch_conditions stock_shot = struck(wedge, square);
    CHECK(flop.launch_angle > stock_shot.launch_angle);
    CHECK(flop.spin > stock_shot.spin);
    CHECK(fly(flop).carry < fly(stock_shot).carry);
}

TEST_CASE("a punch, the ball back and a shorter swing, flies lower than a stock shot") {
    const club_definition& seven = shipped_club("7I");
    const swing_tuning& tuning = shipped_content().tuning.swing;
    const launch_conditions full = struck(seven, stock(seven));
    const launch_conditions punch = struck(seven, club_delivery(seven, setup_dialled(seven, 0.0f, 0.75f, 0.0f, -1.0f), tuning));
    CHECK(punch.launch_angle < full.launch_angle);
    const flight full_flight = fly(full);
    const flight punch_flight = fly(punch);
    CHECK(punch_flight.apex < 0.6f * full_flight.apex);
    CHECK(punch_flight.carry < full_flight.carry);
}

TEST_CASE("a strike off the middle of the face gives the ball less speed, less so for a forgiving club") {
    const club_definition& driver = shipped_club("DR");
    const club_definition& blade = shipped_club("5I");
    REQUIRE(driver.spec.forgiveness > blade.spec.forgiveness);
    const game_tuning& tuning = shipped_content().tuning;
    const auto kept = [&tuning](const club_definition& club) {
        delivery swing = stock(club);
        const float middle = struck(club, swing).ball_speed;
        swing.strike_toe = 0.02f;
        const strike_outcome off =
            impact(club.spec, swing, lie_contact{}, tuning.physics.aero.mass, tuning.scale.ball_physics_radius_meters);
        CHECK(off.centredness < tuning.swing.pure_strike_centredness);
        return off.launch.ball_speed / middle;
    };
    const float driver_kept = kept(driver);
    const float blade_kept = kept(blade);
    CHECK(driver_kept < 1.0f);
    CHECK(blade_kept < driver_kept);
}

TEST_CASE("a wood struck out on the toe turns back towards the heel: gear effect") {
    const club_definition& driver = shipped_club("DR");
    delivery swing = stock(driver);
    const launch_conditions middle = struck(driver, swing);
    swing.strike_toe = 0.015f;
    const launch_conditions toe = struck(driver, swing);
    CHECK(toe.spin_axis_tilt > middle.spin_axis_tilt);
    // An iron has none.
    const club_definition& iron = shipped_club("7I");
    delivery iron_swing = stock(iron);
    const float iron_middle = struck(iron, iron_swing).spin_axis_tilt;
    iron_swing.strike_toe = 0.015f;
    CHECK(struck(iron, iron_swing).spin_axis_tilt == iron_middle);
}

TEST_CASE("opening the face launches every club higher; wide open, a wedge slides up the face: higher still, spinning less") {
    const game_tuning& tuning = shipped_content().tuning;
    for (const club_definition& club : shipped_content().clubs) {
        if (club.club_class == "putter") {
            continue;
        }
        const auto launched = [&](const float open_degrees) {
            shot_setup setup = stock_swing(club, tuning);
            setup.face_open = glm::radians(open_degrees);
            return club_strike(club, setup, ball_on_lie{}, tuning).launch;
        };
        float lower = launched(-club.spec.max_face_closed_degrees).launch_angle;
        for (float open = 5.0f - club.spec.max_face_closed_degrees; open <= club.spec.max_face_open_degrees; open += 5.0f) {
            const float higher = launched(open).launch_angle;
            CHECK(higher > lower);
            lower = higher;
        }
    }
    // A lob wedge opened all the way: a flop's launch, well up, with less spin than square.
    const club_definition& lob = shipped_club("LW");
    shot_setup square = stock_swing(lob, tuning);
    shot_setup open = square;
    open.face_open = glm::radians(lob.spec.max_face_open_degrees);
    const launch_conditions square_launch = club_strike(lob, square, ball_on_lie{}, tuning).launch;
    const launch_conditions open_launch = club_strike(lob, open, ball_on_lie{}, tuning).launch;
    CHECK(open_launch.launch_angle > square_launch.launch_angle + glm::radians(8.0f));
    CHECK(open_launch.spin < square_launch.spin);
}
