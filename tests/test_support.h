#pragma once

// Shared test helpers. Gameplay tests use the shipped tuning, clubs and
// rewards with the small hand-made courses in tests/fixtures, so content
// edits in assets/ do not break them.

#include "game/address_play.h"
#include "game/content_files.h"
#include "game/course_definition.h"
#include "game/course_loader.h"
#include "game/course_session.h"
#include "game/game_content.h"
#include "game/game_input.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/hole_data.h"
#include "game/lighting_loader.h"
#include "game/mode_dispatch.h"
#include "game/prop_loader.h"
#include "game/remote_players.h"
#include "game/play_area.h"
#include "game/round_state.h"
#include "game/text_assets.h"
#include "physics/ball_state.h"
#include "physics/body_collision.h"
#include "physics/solid.h"
#include "physics/terrain.h"
#include "physics/vector_math.h"

#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/vec3.hpp>

inline std::string asset_root() {
    return GOLFPP_ASSETS_DIR;
}

inline std::string fixture_root() {
    return GOLFPP_TEST_FIXTURES_DIR;
}

// The fixture hub course's folder under fixture_root().
inline constexpr const char* fixture_hub_folder = "world/courses/hub";

inline std::string fixture_hole_path(const std::string& name) {
    return fixture_root() + "/" + fixture_hub_folder + "/holes/" + name + ".json";
}

inline bool near(const float a, const float b, const float epsilon = 0.00001f) {
    return std::abs(a - b) <= epsilon;
}

inline bool near(const glm::vec3& a, const glm::vec3& b, const float epsilon = 0.00001f) {
    return glm::length(a - b) <= epsilon;
}

// The shipped content (assets/), loaded once per test run.
inline const game_content& shipped_content() {
    static const game_content content = *load_game_content(asset_root()).content;
    return content;
}

// The shipped strings, styles and font, loaded once per test run.
inline const text_assets& shipped_text_assets() {
    static const text_assets text = *load_text_assets(asset_root());
    return text;
}

// Shipped tuning, clubs and rewards; courses and holes from tests/fixtures.
// The scene colours render tests draw with (tests/fixtures/picture/lighting.json).
inline const scene_colors& test_scene_colors() {
    static const scene_colors colors = load_lighting(fixture_root())->colors;
    return colors;
}

// The shipped props (assets/world/props).
// Relative luminance (Rec. 709), how bright a colour reads.
inline float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

inline const prop_catalog& test_props() {
    static const prop_catalog props = *load_props(asset_root()).props;
    return props;
}

inline game_content fixture_content() {
    game_content content = shipped_content();
    content.asset_root = fixture_root();
    content.courses = load_courses(fixture_root());
    return content;
}

// A course listing `holes`, to parse a course world against.
inline course_definition course_of(const std::vector<std::string>& holes) {
    course_definition course;
    course.id = "course_of";
    course.name = "Course Of";
    course.holes = holes;
    course.world = "world/courses/course_of/world.json";
    return course;
}

inline course_definition fixture_hub_course() {
    return *load_course(fixture_root(), fixture_hub_folder);
}

// A fresh state playing `course` (fixture holes).
inline game_state started_game(const course_definition& course) {
    game_state state = make_game_state(fixture_content(), save_data{});
    start_course(state, course);
    return state;
}

// A fresh state with my ball teed on hole 1 of the fixture course (its start
// is the hole's own tee, unturned, so hole and course coordinates agree).
// A shot the server played: `setup` hit by `account` on `hole` as its
// stroke `stroke`, resting at `rest`, with no dice and nothing in its way.
inline room_shot played_shot(const std::uint64_t account, const int hole, const int stroke, const shot_setup& setup,
                             const glm::vec3& rest, const bool holed) {
    room_shot shot;
    shot.account_id = account;
    shot.hole = hole;
    shot.stroke = stroke;
    shot.record.setup = setup;
    shot.golfer.hand = setup.hand;
    shot.rest = rest;
    shot.holed = holed;
    return shot;
}

inline game_state started_hole() {
    game_state state = started_game(fixture_hub_course());
    tee_up(state, 0, false);
    return state;
}

// Picks my ball out of its cup after holing out from beside the cup, then
// puts the player back where they stood, so the next hole can start there.
inline void pick_up_from_cup(game_state& state) {
    const glm::vec3 standing = state.player.position;
    if (state.ball && state.ball->place == ball_place::in_cup) {
        state.player.position = state.ball->position;
        pick_up_cup_ball(state);
    }
    state.player.position = standing;
}

// Sat at my cart's wheel where I stand, facing my way: the cart brought
// round me, on the ground and standing still.
inline void sit_in_cart(game_state& state) {
    state.cart.pose = cart_around_seat(state.player.position, state.player.yaw, cart_seat::driver, state.tuning.cart);
    state.cart.pose.position = anchor_on_terrain(state.area, state.cart.pose.position);
    state.cart.motion = cart_motion{state.cart.pose.yaw};
    state.cart.vertical = vertical_body{state.cart.pose.position.y, 0.0f, true};
    state.ride = cart_ride{0, cart_seat::driver, std::nullopt, false};
}

// `club`'s stock setup, right-handed, for a ball at the origin lying on
// `lie`, aimed `aim`, swung back `backswing`.
inline shot_setup stock_swing(const club_definition& club, const game_tuning& tuning, const float backswing = 1.0f,
                              const ball_lie lie = ball_lie::fairway, const float aim = 0.0f) {
    shot_setup setup = stock_setup(club, lie, glm::vec3(0.0f), aim, handedness::right, 0.0f, tuning.setup);
    setup.backswing = backswing;
    return setup;
}

// A right-hander's setup of `club` with its dials as given.
inline shot_setup setup_dialled(const club_definition& club, const float aim, const float backswing, const float face_open,
                                const float ball_position) {
    shot_setup setup;
    setup.club_id = club.id;
    setup.aim_angle = aim;
    setup.backswing = backswing;
    setup.face_open = face_open;
    setup.ball_position = ball_position;
    return setup;
}

// `setup` at its club's stock face and ball position for where its ball
// lies, swung back `backswing`: a stock swing, as the dials start.
inline shot_setup stocked(const shot_setup& setup, const game_state& state, const float backswing) {
    shot_setup stock = setup;
    if (const club_definition* club = find_club(state.clubs, setup.club_id)) {
        const shot_setup made =
            stock_setup(*club, lie_at(state.area, setup.ball_start), setup.ball_start, setup.aim_angle, setup.hand, 0.0f,
                        state.tuning.setup);
        stock.face_open = made.face_open;
        stock.ball_position = made.ball_position;
    }
    stock.backswing = backswing;
    return stock;
}

// F: interact with what is in reach; at address, step away from the ball.
inline game_input interact_input() {
    game_input input;
    input.interact = true;
    return input;
}

// Space over the ball: swing the setup.
inline game_input swing_input() {
    game_input input;
    input.swing = true;
    return input;
}

// F at my ball: standing over it at the club's stock setup.
inline void enter_addressing(game_state& state) {
    update_game(state, interact_input(), 0.016f);
}

// Plays the swing on until the ball is struck (or the address ends).
inline void swing_through(game_state& state) {
    for (int frame = 0; frame < 200 && state.mode == game_mode::addressing; ++frame) {
        update_game(state, game_input{}, 0.016f);
    }
}

// Online: the server's answer to the swing over my ball, its shot as set up
// (no dice, nothing in its way), resting at `rest` (else where it flies).
inline void answer_my_swing(game_state& state, const std::optional<glm::vec3>& rest = std::nullopt) {
    room_shot shot = played_shot(state.online.account_id, static_cast<int>(state.ball->hole), state.ball->strokes + 1,
                                 state.address.setup, glm::vec3(0.0f), false);
    shot.rest = rest ? *rest : room_shot_result(state, shot).rest_position;
    state.online.shots.push_back(shot);
}

// Steps up to the ball, swings the stock setup and strikes it (online, the
// server answering it as set up).
inline void hit_selected_club(game_state& state) {
    enter_addressing(state);
    update_game(state, swing_input(), 0.016f);
    if (is_online(state) && state.address.swing_elapsed) {
        answer_my_swing(state);
    }
    swing_through(state);
}

// A straight hole from `tee` to `pin` on a ribbon of the given width.
inline hole_data straight_hole(const glm::vec3& tee, const glm::vec3& pin, const float width) {
    hole_data hole;
    hole.id = "straight";
    hole.name = "Straight";
    hole.par = 3;
    hole.tee_position = tee;
    hole.pin_position = pin;
    hole.spline.control_points = {tee, pin};
    hole.spline.width = width;
    hole.spline.rough_width = width;
    return hole;
}

// A course of `hole` alone, started at its own tee and without land, so it
// keeps its own heights.
inline course_world_definition hole_world(const hole_data& hole) {
    course_world_definition world;
    world.hole_starts = {course_world_hole_start{0, hole.tee_position, 0.0f, 0.0f}};
    return world;
}

inline play_area hole_area(const hole_data& hole, const game_tuning& tuning) {
    return build_course_area({hole}, hole_world(hole), tuning);
}

// Replaces the course with `hole` alone, my ball in play on it where it is
// (on its tee when it was in my pocket).
inline void play_hole(game_state& state, const hole_data& hole) {
    state.course_holes = {hole};
    state.round = start_round(1);
    state.area = hole_area(hole, state.tuning);
    state.world = hole_world(hole);
    state.holes = {course_hole{hole.tee_position, hole.pin_position, hole.par}};
    const glm::vec3 at = state.ball ? state.ball->position : hole.tee_position;
    state.ball = my_ball{0, at, 0, ball_place::teed};
    mark_terrain_render_dirty(state);
}

// A course of the fixture hub's hole 1 alone, being played: holing it
// finishes the round.
inline game_state single_hole_course() {
    game_state state = started_hole();
    play_hole(state, state.course_holes.front());
    return state;
}

// No wind, drag or spin, so ball motion depends only on contact.
inline void still_air(game_state& state) {
    state.tuning.wind = wind_tuning{};
    state.tuning.weather = weather_tuning{};
    state.tuning.physics.aero = ball_aero{};
}

inline float horizontal_speed(const glm::vec3& velocity) {
    return glm::length(horizontal(velocity));
}

// Solids for tests, stopping the ball, people and carts alike.
inline solid test_solid(const solid_shape shape, const solid_kind kind, const glm::vec3& base, const float radius,
                        const float height) {
    solid thing;
    thing.shape = shape;
    thing.kind = kind;
    thing.base = base;
    thing.radius = radius;
    thing.height = height;
    thing.stops_ball = true;
    thing.stops_walker = true;
    thing.stops_cart = true;
    return thing;
}

inline solid test_post(const glm::vec3& base, const float radius, const float height = 2.0f) {
    return test_solid(solid_shape::cylinder, solid_kind::pole, base, radius, height);
}

inline solid test_wall(const glm::vec3& a, const glm::vec3& b, const float half_thickness, const float height = 2.0f) {
    solid thing = test_solid(solid_shape::wall, solid_kind::net, a, half_thickness, height);
    thing.end = b;
    return thing;
}

// `half_length` along unit `along`, `half_width` across, from `base` up `height`.
inline solid test_box(const glm::vec3& base, const glm::vec3& along, const float half_length, const float half_width,
                      const float height = 1.0f) {
    solid thing = test_solid(solid_shape::box, solid_kind::tee_box, base, 0.0f, height);
    thing.along = along;
    thing.half_length = half_length;
    thing.half_width = half_width;
    return thing;
}

// The area's solids of `kind`, in their order.
inline std::vector<solid> solids_of(const play_area& area, const solid_kind kind) {
    std::vector<solid> found;
    for (const solid& thing : area.solids) {
        if (thing.kind == kind) {
            found.push_back(thing);
        }
    }
    return found;
}

// Whether a solid of `kind` stands with its foot at `foot` (XZ).
inline bool solid_stands_at(const play_area& area, const solid_kind kind, const glm::vec3& foot) {
    for (const solid& thing : area.solids) {
        if (thing.kind == kind && horizontal_distance(thing.base, foot) < 0.001f) {
            return true;
        }
    }
    return false;
}

// The area's solids replaced by `solids`, on a fresh grid.
inline void set_solids(play_area& area, std::vector<solid> solids) {
    area.solids = std::move(solids);
    area.solids_grid = build_solid_grid(area.solids, 16.0f);
}

// `body` moved among `solids` alone, by what stops `mover`.
inline body_move move_among(const std::vector<solid>& solids, const solid_mover mover, const ground_body& body,
                            const glm::vec3& from, const glm::vec3& to) {
    static const std::vector<solid> nothing_moving;
    const solid_grid grid = build_solid_grid(solids, 16.0f);
    return move_body(body_obstacles{solids, grid, nothing_moving, mover}, body, from, to);
}

// A physics ball of the tuning's size, still, at the origin.
inline ball_state still_ball(const game_state& state) {
    ball_state ball;
    ball.radius = state.tuning.scale.ball_physics_radius_meters;
    return ball;
}

inline glm::vec3 resting_on_terrain(const game_state& state, const glm::vec3& position) {
    const terrain_sample sample = sample_area(state.area, position);
    return sample.point + ground_normal(sample.normal) * state.tuning.scale.ball_physics_radius_meters;
}

// An unturned ellipse's outline as a zone, 64 points round it; a circle
// when the radii are equal.
inline material_zone ellipse_zone(const material_zone_type type,
                                  const glm::vec3& center,
                                  const float radius_x,
                                  const float radius_z) {
    constexpr int points = 64;
    std::vector<glm::vec2> outline;
    for (int i = 0; i < points; ++i) {
        const float angle = 6.283185307f * static_cast<float>(i) / static_cast<float>(points);
        outline.emplace_back(center.x + std::cos(angle) * radius_x, center.z + std::sin(angle) * radius_z);
    }
    return make_material_zone(type, std::move(outline));
}

inline material_zone circle_zone(const material_zone_type type, const glm::vec3& center, const float radius) {
    return ellipse_zone(type, center, radius, radius);
}

// A ribbon with no zones (bunker and water depths don't matter then).
inline terrain_mesh plain_terrain_mesh(const terrain_spline& spline) {
    return build_terrain_mesh(spline);
}

inline terrain_sample sample_spline(const terrain_spline& spline, const glm::vec3& position, const float fallback_y) {
    return sample_terrain_mesh(plain_terrain_mesh(spline), position, fallback_y);
}
