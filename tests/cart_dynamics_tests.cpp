#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/play_area.h"
#include "physics/cart_dynamics.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/trigonometric.hpp>

namespace {
const cart_spec& shipped_cart() {
    const game_content& content = shipped_content();
    return *content.items.find(content.default_loadout.cart)->cart;
}

const cart_surfaces_tuning& cart_surfaces() {
    return shipped_content().tuning.surfaces.cart;
}

// Ground of `surface` (dry) under a cart facing +Z (yaw 0): rising `grade`
// (rise over run) ahead and `side_degrees` towards its left (+X).
cart_ground ground_of(const cart_surface_tuning& surface, const float grade = 0.0f, const float side_degrees = 0.0f) {
    const float rise_left = std::tan(glm::radians(side_degrees));
    cart_ground ground;
    ground.normal = glm::normalize(glm::vec3(-rise_left, 1.0f, -grade));
    ground.rolling_resistance = surface.rolling_resistance;
    ground.grip = surface.grip;
    return ground;
}

// The cart after `seconds` of the same pedal and steering on the same ground.
cart_motion drive_for(cart_motion motion, const cart_controls& controls, const cart_ground& ground, const float seconds) {
    const int steps = static_cast<int>(std::lround(seconds / cart_step_seconds));
    for (int i = 0; i < steps; ++i) {
        motion = step_cart(motion, controls, ground, shipped_cart(), cart_step_seconds);
    }
    return motion;
}

constexpr cart_controls full_throttle{1.0f, 0.0f};
}

TEST_CASE("the shipped cart is a real electric two-seater: its build within the reference's ranges") {
    const cart_spec& cart = shipped_cart();
    CHECK(cart.motor_power_w >= 3000.0f);
    CHECK(cart.motor_power_w <= 4000.0f);
    CHECK(cart.top_speed_mps >= 5.3f);
    CHECK(cart.top_speed_mps <= 6.7f);
    CHECK(cart.track_m >= 0.9f);
    CHECK(cart.track_m <= 1.0f);
    CHECK(cart.cg_height_m >= 0.6f);
    CHECK(cart.cg_height_m <= 0.7f);
    CHECK(cart_stability(cart) >= 0.7f);
    CHECK(cart_stability(cart) <= 0.8f);
    const cart_surfaces_tuning& surfaces = cart_surfaces();
    CHECK(near(surfaces.path.rolling_resistance, 0.015f, 0.001f));
    CHECK(near(surfaces.fairway.rolling_resistance, 0.05f, 0.001f));
    CHECK(surfaces.rough.rolling_resistance >= 0.08f);
    CHECK(surfaces.rough.rolling_resistance <= 0.10f);
    // Soft bunker sand: past the dry-sand range, as the reference says soft sand is more.
    CHECK(surfaces.bunker.rolling_resistance >= 0.30f);
    CHECK(near(surfaces.path.grip, 0.8f, 0.001f));
    CHECK(surfaces.fairway.grip >= 0.5f);
    CHECK(surfaces.fairway.grip <= 0.6f);
    const float wet_grass = surfaces.fairway.grip * surfaces.fairway.wet_grip_scale;
    CHECK(wet_grass >= 0.3f);
    CHECK(wet_grass <= 0.4f);
}

TEST_CASE("the game's carts run a named number of times faster than real time, about twice walking") {
    const game_tuning& tuning = shipped_content().tuning;
    const float game_speed = game_top_speed(shipped_cart(), tuning.cart);
    CHECK(near(game_speed, shipped_cart().top_speed_mps * tuning.cart.time_scale, 0.0001f));
    const float walking = tuning.player.walk_speed * tuning.scale.meters_per_world_unit;
    CHECK(game_speed > 1.9f * walking);
    CHECK(game_speed < 2.1f * walking);
}

TEST_CASE("on the flat a cart reaches its top speed and no more") {
    cart_motion motion;
    float fastest = 0.0f;
    for (int second = 0; second < 10; ++second) {
        motion = drive_for(motion, full_throttle, ground_of(cart_surfaces().fairway), 1.0f);
        fastest = std::max(fastest, cart_forward_speed(motion));
    }
    CHECK(near(cart_forward_speed(motion), shipped_cart().top_speed_mps, 0.001f));
    CHECK(fastest <= shipped_cart().top_speed_mps + 0.0001f);
    CHECK(motion.yaw == 0.0f);
    // Backing up, governed to its reverse share.
    const cart_motion backing = drive_for(cart_motion{}, cart_controls{-1.0f, 0.0f}, ground_of(cart_surfaces().path), 15.0f);
    CHECK(near(cart_forward_speed(backing), -shipped_cart().top_speed_mps * shipped_cart().reverse_share, 0.001f));
}

TEST_CASE("up a 25 % grade a cart is slower but keeps moving, and down one it holds its top speed") {
    const cart_ground up = ground_of(cart_surfaces().fairway, 0.25f);
    const cart_motion climbing = drive_for(cart_motion{}, full_throttle, up, 20.0f);
    CHECK(cart_forward_speed(climbing) > 1.0f);
    CHECK(cart_forward_speed(climbing) < 0.7f * shipped_cart().top_speed_mps);
    // Its motor's power holds it there: the force it needs times the speed.
    const float g = gravity_meters_per_second2;
    const float slope = std::atan(0.25f);
    const float needed = shipped_cart().mass_kg * g * (std::sin(slope) + up.rolling_resistance * std::cos(slope));
    CHECK(near(cart_forward_speed(climbing), shipped_cart().motor_power_w / needed, 0.05f));
    // All of its power, not all of its force.
    CHECK(near(climbing.load * shipped_cart().max_drive_force_n * cart_forward_speed(climbing), shipped_cart().motor_power_w,
               0.02f * shipped_cart().motor_power_w));

    const cart_ground down = ground_of(cart_surfaces().fairway, -0.25f);
    const cart_motion descending = drive_for(cart_motion{}, full_throttle, down, 10.0f);
    CHECK(near(cart_forward_speed(descending), shipped_cart().top_speed_mps, 0.001f));
    // Let go, the motor brakes it to a stop and holds it there.
    const cart_motion let_go = drive_for(descending, cart_controls{}, down, 5.0f);
    CHECK(cart_forward_speed(let_go) == 0.0f);
}

TEST_CASE("a cart is quicker on a path than on grass, and crawls in sand, where a slope stops it") {
    const cart_motion path = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().path, 0.15f), 10.0f);
    const cart_motion grass = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().rough, 0.15f), 10.0f);
    CHECK(cart_forward_speed(path) > cart_forward_speed(grass) + 0.3f);
    // From standing, the path pulls away sooner.
    const cart_motion path_start = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().path), 0.5f);
    const cart_motion grass_start = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().rough), 0.5f);
    CHECK(cart_forward_speed(path_start) > cart_forward_speed(grass_start));

    const cart_motion sand = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().bunker), 10.0f);
    CHECK(cart_forward_speed(sand) > 0.5f);
    CHECK(cart_forward_speed(sand) < 0.5f * shipped_cart().top_speed_mps);
    // Up a bunker's side it does not move at all: stuck.
    const cart_motion stuck = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().bunker, 0.2f), 5.0f);
    CHECK(cart_forward_speed(stuck) <= 0.0f);
    CHECK(stuck.condition == cart_condition::upright);
}

TEST_CASE("standing on a side slope a cart rolls over near 36 degrees, by its static stability factor") {
    const cart_spec& cart = shipped_cart();
    const float rolls_at = glm::degrees(std::atan(cart_stability(cart)));
    CHECK(rolls_at >= 35.0f);
    CHECK(rolls_at <= 38.0f);
    const cart_motion holds = drive_for(cart_motion{}, cart_controls{}, ground_of(cart_surfaces().path, 0.0f, rolls_at - 1.0f), 2.0f);
    CHECK(holds.condition == cart_condition::upright);
    // Rising to its left, it rolls over its right wheels.
    const cart_motion rolls = drive_for(cart_motion{}, cart_controls{}, ground_of(cart_surfaces().path, 0.0f, rolls_at + 1.0f), 2.0f);
    CHECK(rolls.condition == cart_condition::on_right_side);
    CHECK(rolls.velocity == glm::vec2(0.0f));
    // Only once its wheels have been up for tip_seconds.
    const cart_motion lifting = drive_for(cart_motion{}, cart_controls{}, ground_of(cart_surfaces().path, 0.0f, rolls_at + 1.0f),
                                          0.5f * cart.tip_seconds);
    CHECK(lifting.condition == cart_condition::upright);
    CHECK(lifting.lift_seconds > 0.0f);
}

TEST_CASE("a hard turn at speed tips a cart on a side slope, never on flat grass, where it slides wide past its grip") {
    const cart_controls hard_left{1.0f, 1.0f};
    const cart_motion at_speed = drive_for(cart_motion{}, full_throttle, ground_of(cart_surfaces().fairway), 8.0f);
    REQUIRE(near(cart_forward_speed(at_speed), shipped_cart().top_speed_mps, 0.01f));

    // On the flat the tyres let go before the wheels lift: it slides round wider than it steers.
    cart_motion flat = at_speed;
    float widest = 0.0f;
    for (int i = 0; i < 240; ++i) {
        flat = step_cart(flat, hard_left, ground_of(cart_surfaces().fairway), shipped_cart(), cart_step_seconds);
        CHECK(flat.condition == cart_condition::upright);
        widest = std::max(widest, std::abs(flat.yaw_rate));
    }
    const float steered = shipped_cart().top_speed_mps * std::tan(glm::radians(shipped_cart().max_steer_degrees)) /
        shipped_cart().wheelbase_m;
    CHECK(widest < 0.8f * steered);

    // Turning left with the ground falling away to its right: over onto its right side.
    cart_motion sloped = at_speed;
    for (int i = 0; i < 240 && sloped.condition == cart_condition::upright; ++i) {
        sloped = step_cart(sloped, hard_left, ground_of(cart_surfaces().fairway, 0.0f, 15.0f), shipped_cart(), cart_step_seconds);
    }
    CHECK(sloped.condition == cart_condition::on_right_side);
    // Driving straight across the same slope it stays up.
    const cart_motion across = drive_for(at_speed, full_throttle, ground_of(cart_surfaces().fairway, 0.0f, 15.0f), 2.0f);
    CHECK(across.condition == cart_condition::upright);
}

TEST_CASE("water deeper than its wade depth sinks a cart; shallower it drives through") {
    cart_ground shallow = ground_of(cart_surfaces().water);
    shallow.water_depth = 0.8f * shipped_cart().wade_depth_m;
    const cart_motion wading = drive_for(cart_motion{}, full_throttle, shallow, 2.0f);
    CHECK(wading.condition == cart_condition::upright);
    CHECK(cart_forward_speed(wading) > 0.0f);
    cart_ground deep = shallow;
    deep.water_depth = 1.2f * shipped_cart().wade_depth_m;
    const cart_motion sunk = drive_for(wading, full_throttle, deep, cart_step_seconds);
    CHECK(sunk.condition == cart_condition::sunk);
    CHECK(sunk.velocity == glm::vec2(0.0f));
    // Sunk or tipped, it goes nowhere.
    CHECK(drive_for(sunk, full_throttle, ground_of(cart_surfaces().path), 1.0f).velocity == glm::vec2(0.0f));
}

TEST_CASE("a cart on a course's ground: a path on a cart road, its material off it, soaked by the weather, and the pond's depth") {
    const game_tuning& tuning = shipped_content().tuning;
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 40.0f);
    hole.material_zones = {circle_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 120.0f), 12.0f),
                  circle_zone(material_zone_type::bunker, glm::vec3(0.0f, 0.0f, 60.0f), 6.0f)};
    const play_area area = hole_area(hole, tuning);
    const std::vector<course_world_cart_road> roads{course_world_cart_road{3.0f, {glm::vec3(-10.0f, 0.0f, 20.0f), glm::vec3(10.0f, 0.0f, 20.0f)}}};
    const cart_surfaces_tuning& surfaces = tuning.surfaces.cart;

    CHECK(cart_ground_at(area, roads, tuning, 0.0f, glm::vec3(0.0f, 0.0f, 20.0f)).rolling_resistance == surfaces.path.rolling_resistance);
    // Past the road's half width it is the ground's own.
    CHECK(cart_ground_at(area, roads, tuning, 0.0f, glm::vec3(0.0f, 0.0f, 22.0f)).rolling_resistance == surfaces.fairway.rolling_resistance);
    CHECK(cart_ground_at(area, roads, tuning, 0.0f, glm::vec3(0.0f, 0.0f, 60.0f)).rolling_resistance == surfaces.bunker.rolling_resistance);
    const cart_ground soaked = cart_ground_at(area, roads, tuning, 1.0f, glm::vec3(0.0f, 0.0f, 40.0f));
    CHECK(near(soaked.grip, surfaces.fairway.grip * surfaces.fairway.wet_grip_scale, 0.0001f));
    const cart_ground pond = cart_ground_at(area, roads, tuning, 0.0f, glm::vec3(0.0f, 0.0f, 120.0f));
    CHECK(pond.water_depth > shipped_cart().wade_depth_m);
    CHECK(cart_ground_at(area, roads, tuning, 0.0f, glm::vec3(0.0f, 0.0f, 40.0f)).water_depth == 0.0f);
}

namespace {
// The front wheels' share of full lock that steers a cart at `motion` towards
// `target` (pure pursuit: the arc through it).
float steer_towards(const cart_motion& motion, const glm::vec3& at, const glm::vec3& target) {
    const glm::vec3 to = horizontal(target - at);
    const float distance = glm::length(to);
    if (distance < 0.01f) {
        return 0.0f;
    }
    const float off = wrap_angle(yaw_towards(at, target) - motion.yaw);
    if (std::abs(off) > glm::half_pi<float>()) {
        return off > 0.0f ? 1.0f : -1.0f;  // behind: round at full lock
    }
    const float curvature = 2.0f * std::sin(off) / distance;
    const float wheels = std::atan(curvature * shipped_cart().wheelbase_m);
    return std::clamp(wheels / glm::radians(shipped_cart().max_steer_degrees), -1.0f, 1.0f);
}

// A driver keeping to a road from its start to its end: where along it they
// are (a segment and the share of it), never going back, so a road that
// loops is driven once round.
struct road_follower {
    const std::vector<glm::vec3>& road;
    std::size_t segment = 0;
    float along = 0.0f;

    // On to the nearest point to `at` on this segment or the next two.
    void follow(const glm::vec3& at) {
        float best = 1.0e30f;
        const std::size_t last = std::min(segment + 3, road.size() - 1);
        for (std::size_t i = segment; i < last; ++i) {
            const glm::vec3 a = horizontal(road[i]);
            const glm::vec3 ab = horizontal(road[i + 1]) - a;
            const float t = clamp01(glm::dot(horizontal(at) - a, ab) / std::max(glm::dot(ab, ab), 1.0e-6f));
            const float distance = glm::length(horizontal(at) - (a + ab * t));
            // A later segment wins a tie: at a corner both meet it.
            const bool nearer = i > segment ? distance <= best : distance < best && t >= along;
            if (nearer) {
                best = distance;
                segment = i;
                along = t;
            }
        }
    }

    bool at_end() const {
        return segment + 2 == road.size() && along >= 0.99f;
    }

    // The point `ahead` metres on along the road.
    glm::vec3 ahead_of_me(const float ahead) const {
        float left = ahead;
        glm::vec3 point = glm::mix(road[segment], road[segment + 1], along);
        for (std::size_t i = segment; i + 1 < road.size(); ++i) {
            const float span = horizontal_distance(point, road[i + 1]);
            if (span >= left) {
                return point + (road[i + 1] - point) * (left / std::max(span, 1.0e-6f));
            }
            left -= span;
            point = road[i + 1];
        }
        return road.back();
    }
};
}

namespace {
// `road` cut where it turns back on itself (more than 150 degrees at a
// point), which no cart drives round: some imported roads fold back along
// their own line.
std::vector<std::vector<glm::vec3>> road_stretches(const std::vector<glm::vec3>& road) {
    std::vector<std::vector<glm::vec3>> stretches{{road.front()}};
    for (std::size_t i = 1; i < road.size(); ++i) {
        stretches.back().push_back(road[i]);
        if (i + 1 < road.size()) {
            const float turn = wrap_angle(yaw_towards(road[i], road[i + 1]) - yaw_towards(road[i - 1], road[i]));
            if (std::abs(turn) > glm::radians(150.0f)) {
                stretches.push_back({road[i]});
            }
        }
    }
    return stretches;
}
}

TEST_CASE("driving every shipped course's cart roads at full throttle never tips the cart") {
    std::vector<std::string> tipped;
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        for (const course_world_cart_road& road : state.world.cart_roads) {
            for (const std::vector<glm::vec3>& stretch : road_stretches(road.polyline)) {
                if (stretch.size() < 2) {
                    continue;
                }
                road_follower driver{stretch};
                glm::vec3 at = stretch.front();
                cart_motion motion{yaw_towards(stretch[0], stretch[1])};
                for (int step = 0; step < 120 * 600 && !driver.at_end(); ++step) {
                    const cart_ground ground = cart_ground_at(state.area, state.world.cart_roads, state.tuning, 0.0f, at);
                    const float steer = steer_towards(motion, at, driver.ahead_of_me(8.0f));
                    motion = step_cart(motion, cart_controls{1.0f, steer}, ground, shipped_cart(), cart_step_seconds);
                    if (cart_tipped(motion.condition)) {
                        tipped.push_back(course.id + " at " + std::to_string(at.x) + ", " + std::to_string(at.z));
                        break;
                    }
                    at += glm::vec3(motion.velocity.x, 0.0f, motion.velocity.y) * cart_step_seconds;
                    driver.follow(at);
                }
                CHECK((driver.at_end() || cart_tipped(motion.condition)));
            }
        }
    }
    for (const std::string& where : tipped) {
        std::cerr << "tipped on a cart road: " << where << "\n";
    }
    CHECK(tipped.empty());
}
