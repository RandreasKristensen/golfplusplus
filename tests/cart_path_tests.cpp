#include "doctest.h"

#include "game/area_solids.h"
#include "game/cart_path.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/tee_box.h"
#include "game/text_ids.h"
#include "game/way_sign.h"
#include "game/shot_simulation.h"
#include "physics/solid.h"
#include "physics/vector_math.h"
#include "renderer/cart_path_mesh.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/hole_sign_face.h"
#include "renderer/hud_overlay.h"
#include "renderer/overlay_batch.h"

#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace {
const game_tuning& tuning() {
    return shipped_content().tuning;
}

// A straight flat hole from the origin down +z, its green round the pin, a
// pond beside it at x = 30, and a path across it at z = 100 that runs on
// over the pond.
hole_data path_hole() {
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f);
    hole.spline.rough_width = 90.0f;
    hole.material_zones = {circle_zone(material_zone_type::green, hole.pin_position, 12.0f),
                           circle_zone(material_zone_type::water, glm::vec3(30.0f, 0.0f, 100.0f), 6.0f)};
    return hole;
}

const course_world_cart_road across_road{2.5f, {glm::vec3(-20.0f, 0.0f, 100.0f), glm::vec3(40.0f, 0.0f, 100.0f)}};

play_area area_with_roads(const std::vector<hole_data>& holes, const std::vector<course_world_cart_road>& roads) {
    course_world_definition world;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        world.hole_starts.push_back(course_world_hole_start{static_cast<int>(i), holes[i].tee_position, 0.0f, 0.0f});
    }
    world.cart_roads = roads;
    return build_course_area(holes, world, tuning());
}

const solid* path_under(const play_area& area, const glm::vec3& at) {
    const terrain_sample sample = sample_area(area, at);
    if (sample.top_solid < 0) {
        return nullptr;
    }
    const solid& thing = area.solids[static_cast<std::size_t>(sample.top_solid)];
    return thing.kind == solid_kind::path ? &thing : nullptr;
}

ball_state ball_at(const glm::vec3& position, const glm::vec3& velocity) {
    ball_state ball;
    ball.radius = tuning().scale.ball_physics_radius_meters;
    ball.position = position;
    ball.velocity = velocity;
    return ball;
}

// How high a ball dropped from `height` over `spot` rises after its first bounce.
float rebound(const play_area& area, const glm::vec3& spot, const float height) {
    const shot_course course{area, shot_hole{glm::vec3(0.0f, 0.0f, 300.0f)}, weather_state{}};
    const float floor = terrain_height(area, spot);
    ball_state ball = ball_at(glm::vec3(spot.x, floor + height, spot.z), glm::vec3(0.0f));
    terrain_sample ground = sample_area(area, ball.position);
    bool bounced = false;
    float highest = 0.0f;
    for (int step = 0; step < 2000; ++step) {
        const shot_step next = step_shot(ball, ground, course, tuning(), shot_step_seconds, step);
        if (bounced && next.landed) {
            break;
        }
        bounced = bounced || next.landed;
        ball = next.ball;
        ground = next.ground;
        if (bounced) {
            highest = std::max(highest, ball.position.y - ball.radius - floor);
        }
    }
    return highest;
}
}

TEST_CASE("a cart path's hard top lies its lift over the ground everywhere, never floating or buried, its slabs in the one list of solids") {
    // The hole climbs 30 m over its 300: the path across it runs over a slope.
    hole_data hole = path_hole();
    hole.pin_position.y = 30.0f;
    hole.spline.control_points = {hole.tee_position, hole.pin_position};
    const play_area area = area_with_roads({hole}, {across_road});
    REQUIRE(area.cart_paths.size() == 1U);

    const solid* slab = path_under(area, glm::vec3(0.0f, 0.0f, 100.0f));
    REQUIRE(slab != nullptr);
    CHECK(slab->shape == solid_shape::slab);
    CHECK(slab->walkable);
    CHECK(!stops(*slab, solid_mover::walker));
    CHECK(!stops(*slab, solid_mover::cart));
    const float lift = tuning().cart_path.top_lift;
    for (float x = -19.0f; x <= 20.0f; x += 0.37f) {
        for (const float z : {98.8f, 99.5f, 100.0f, 100.6f, 101.2f}) {
            const glm::vec3 at(x, 0.0f, z);
            const terrain_sample ground = sample_terrain_mesh(area.ground, at, area.ground_y);
            const terrain_sample top = sample_area(area, at);
            REQUIRE(path_under(area, at) != nullptr);
            CHECK(near(top.point.y, ground.point.y + lift, 0.0005f));
            // Its normal is the ground's along it, level across it (the road runs along x).
            CHECK(near(top.normal.x, ground.normal.x / glm::length(glm::vec2(ground.normal.x, ground.normal.y)), 0.0001f));
            CHECK(near(top.normal.z, 0.0f, 0.0001f));
        }
    }
    // Nothing past its edge.
    CHECK(path_under(area, glm::vec3(0.0f, 0.0f, 101.4f)) == nullptr);
    CHECK(sample_area(area, glm::vec3(0.0f, 0.0f, 103.0f)).top_solid == -1);

    // Over the pond it is the bridge: over the water, never in it.
    const terrain_sample bridge = sample_area(area, glm::vec3(30.0f, 0.0f, 100.0f));
    CHECK(bridge.material == terrain_material::water);
    CHECK(bridge.top_solid >= 0);
    CHECK(near(bridge.point.y, bridge.water_level + lift, 0.0005f));
}

TEST_CASE("a ball bounces higher off a cart path than off the turf beside it, by the path's restitution") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const float drop = 2.0f;
    const float off_path = rebound(area, glm::vec3(0.0f, 0.0f, 110.0f), drop);
    const float on_path = rebound(area, glm::vec3(0.0f, 0.0f, 100.0f), drop);
    const float restitution = tuning().solids.path.restitution;
    // Straight down onto a rigid top: up again to e^2 of the drop, less a little to the air.
    CHECK(near(on_path, restitution * restitution * drop, 0.1f * restitution * restitution * drop));
    CHECK(on_path > 2.0f * off_path);
}

TEST_CASE("carts get the path's numbers on a cart path's top, the ground's off it, and no water on its bridge") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const cart_surfaces_tuning& surfaces = tuning().surfaces.cart;
    CHECK(cart_ground_at(area, tuning(), 0.0f, glm::vec3(0.0f, 0.0f, 100.0f)).rolling_resistance ==
          surfaces.path.rolling_resistance);
    CHECK(cart_ground_at(area, tuning(), 0.0f, glm::vec3(0.0f, 0.0f, 100.0f)).grip == surfaces.path.grip);
    CHECK(cart_ground_at(area, tuning(), 0.0f, glm::vec3(0.0f, 0.0f, 104.0f)).rolling_resistance ==
          surfaces.fairway.rolling_resistance);
    CHECK(surfaces.path.rolling_resistance < surfaces.rough.rolling_resistance);
    CHECK(surfaces.path.grip > surfaces.rough.grip);
    CHECK(cart_ground_at(area, tuning(), 0.0f, anchor_on_terrain(area, glm::vec3(30.0f, 0.0f, 100.0f))).immersion == 0.0f);
    CHECK(cart_ground_at(area, tuning(), 0.0f, anchor_on_terrain(area, glm::vec3(30.0f, 0.0f, 104.0f))).immersion > 0.0f);
}

namespace {
const cart_spec& shipped_cart_spec() {
    return *shipped_content().items.find(shipped_content().default_loadout.cart)->cart;
}

// How far under the pond's surface wheels at `cart`'s pose are, in metres.
float immersion_of(const cart_mover& cart, const terrain_sample& pond) {
    return (pond.water_level - cart.pose.position.y) * tuning().scale.meters_per_world_unit;
}
}

TEST_CASE("a cart in the air over deep water falls on, and sinks only once its wheels are under deeper than it wades") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const cart_spec& spec = shipped_cart_spec();
    const std::vector<solid> none;
    const cart_world world{area, tuning(), spec, 0.0f, 1.0f, none, nullptr};
    const terrain_sample pond = sample_area(area, glm::vec3(30.0f, 0.0f, 104.0f));
    REQUIRE(pond.material == terrain_material::water);
    REQUIRE((pond.water_level - pond.point.y) * tuning().scale.meters_per_world_unit > 2.0f * spec.wade_depth_m);

    // A metre over the water: the next step it is still upright, falling.
    cart_mover cart;
    cart.pose = cart_pose{glm::vec3(30.0f, pond.water_level + 1.0f / tuning().scale.meters_per_world_unit, 104.0f), 0.0f};
    cart.vertical = vertical_body{cart.pose.position.y, 0.0f, false};
    const cart_mover next = step_cart_mover(cart, cart_controls{}, world).after;
    CHECK(next.motion.condition == cart_condition::upright);
    CHECK(!next.vertical.grounded);
    CHECK(next.pose.position.y < cart.pose.position.y);

    // On down until it sinks: only as its wheels pass its wade depth under.
    cart_mover falling = next;
    float deepest_upright = immersion_of(cart, pond);
    for (int step = 0; step < 1000 && falling.motion.condition == cart_condition::upright; ++step) {
        deepest_upright = std::max(deepest_upright, immersion_of(falling, pond));
        const cart_mover after = step_cart_mover(falling, cart_controls{}, world).after;
        if (after.motion.condition == cart_condition::upright) {
            falling = after;
        } else {
            falling.motion = after.motion;
        }
    }
    REQUIRE(falling.motion.condition == cart_condition::sunk);
    CHECK(immersion_of(falling, pond) > spec.wade_depth_m);
    CHECK(falling.pose.position.y > pond.point.y);  // in the water, not yet on its floor
    CHECK(deepest_upright < spec.wade_depth_m + 0.1f);
}

TEST_CASE("a cart driven off its bridge's side drops into the pond and sinks there, not at the bridge's height") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const cart_spec& spec = shipped_cart_spec();
    const std::vector<solid> none;
    const cart_world world{area, tuning(), spec, 0.0f, 1.0f, none, nullptr};
    const terrain_sample bridge = sample_area(area, glm::vec3(30.0f, 0.0f, 100.0f));
    REQUIRE(path_under(area, bridge.point) != nullptr);
    const terrain_sample pond = sample_area(area, glm::vec3(30.0f, 0.0f, 104.0f));

    // On the bridge, facing across it, then full ahead off its edge.
    cart_mover cart;
    cart.pose = cart_pose{bridge.point, yaw_towards(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f))};
    cart.motion.yaw = cart.pose.yaw;
    cart.vertical = vertical_body{bridge.point.y, 0.0f, true};
    bool left_bridge = false;
    bool upright_off_it = false;
    for (int step = 0; step < 2000 && cart.motion.condition == cart_condition::upright; ++step) {
        const bool over_open_water = path_under(area, cart.pose.position) == nullptr &&
            sample_area(area, cart.pose.position).material == terrain_material::water;
        cart_mover after = step_cart_mover(cart, cart_controls{1.0f, 0.0f}, world).after;
        if (over_open_water && !left_bridge) {
            left_bridge = true;
            upright_off_it = after.motion.condition == cart_condition::upright;
        }
        cart = after;
    }
    CHECK(left_bridge);
    CHECK(upright_off_it);
    REQUIRE(cart.motion.condition == cart_condition::sunk);
    CHECK(immersion_of(cart, pond) > spec.wade_depth_m);
    CHECK(cart.pose.position.y < bridge.point.y);
}

TEST_CASE("a cart drives along its bridge over the pond and off the far side, upright and dry") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const std::vector<solid> none;
    const cart_world world{area, tuning(), shipped_cart_spec(), 0.0f, 1.0f, none, nullptr};
    const glm::vec3 start = anchor_on_terrain(area, glm::vec3(16.0f, 0.0f, 100.0f));
    REQUIRE(path_under(area, start) != nullptr);
    cart_mover cart;
    cart.pose = cart_pose{start, yaw_towards(glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f))};
    cart.motion.yaw = cart.pose.yaw;
    cart.vertical = vertical_body{start.y, 0.0f, true};
    bool crossed_water = false;
    for (int step = 0; step < 2000 && cart.pose.position.x < 39.0f; ++step) {
        cart = step_cart_mover(cart, cart_controls{1.0f, 0.0f}, world).after;
        REQUIRE(cart.motion.condition == cart_condition::upright);
        crossed_water = crossed_water || sample_area(area, cart.pose.position).material == terrain_material::water;
    }
    CHECK(crossed_water);
    CHECK(cart.pose.position.x >= 39.0f);
}

TEST_CASE("a cart takes a path's edge one pair of wheels at a time: half on, its grip is halfway") {
    const play_area area = area_with_roads({path_hole()}, {across_road});
    const game_tuning& rules = tuning();
    const cart_spec& spec = *shipped_content().items.find(shipped_content().default_loadout.cart)->cart;
    const std::vector<solid> none;
    const cart_world world{area, rules, spec, 0.0f, 1.0f, none, nullptr};
    const float on_path = rules.surfaces.cart.path.grip;
    const float off_path = cart_ground_at(area, rules, 0.0f, glm::vec3(-10.0f, 0.0f, 90.0f)).grip;
    REQUIRE(on_path > off_path);
    // Facing the path (+z) across its near edge: the front wheels on it, the
    // rear ones on the fairway.
    const float edge = across_road.polyline.front().z - across_road.width * 0.5f;
    const float middle_ahead = 0.5f * (rules.cart.body_front - rules.cart.body_back);
    const float front = middle_ahead + 0.5f * spec.wheelbase_m;
    const float rear = middle_ahead - 0.5f * spec.wheelbase_m;
    const float seat = edge - 0.5f * (front + rear);
    const cart_pose half_on{glm::vec3(-10.0f, 0.0f, seat), yaw_towards(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f))};
    CHECK(near(cart_ground_under(half_on, world).grip, 0.5f * (on_path + off_path), 0.001f));
    // Wholly on it, and wholly off it.
    CHECK(near(cart_ground_under(cart_pose{glm::vec3(-10.0f, 0.0f, 99.5f), half_on.yaw}, world).grip, on_path, 0.001f));
    CHECK(near(cart_ground_under(cart_pose{glm::vec3(-10.0f, 0.0f, 92.0f), half_on.yaw}, world).grip, off_path, 0.001f));
}

TEST_CASE("walking onto a cart path, I stand on its top") {
    game_state state = started_game(fixture_hub_course());
    REQUIRE(!state.area.cart_paths.empty());
    const cart_path& path = state.area.cart_paths.front();
    const glm::vec3 joint = path.joints[path.joints.size() / 2];
    const glm::vec3 along = safe_normalize(horizontal(path.joints[path.joints.size() / 2 + 1] - joint), glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::vec3 across(-along.z, 0.0f, along.x);
    state.player.position = anchor_on_terrain(state.area, joint + across * (path.half_width + 3.0f));
    state.player.yaw = yaw_towards(state.player.position, joint);
    game_input walk;
    walk.forward_held = true;
    bool on_top = false;
    for (int i = 0; i < 60 && !on_top; ++i) {
        update_game(state, walk, 0.05f);
        on_top = path_under(state.area, state.player.position) != nullptr &&
            near(state.player.position.y, terrain_height(state.area, state.player.position), 0.001f);
    }
    CHECK(on_top);
}

namespace {
// Two holes side by side, 80 apart: hole 1 up +z to its pin at z = 200,
// hole 2 back down from its tee at z = 220.
std::vector<hole_data> two_holes() {
    hole_data first = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 30.0f);
    hole_data second = straight_hole(glm::vec3(80.0f, 0.0f, 220.0f), glm::vec3(80.0f, 0.0f, 20.0f), 30.0f);
    for (hole_data* hole : {&first, &second}) {
        hole->spline.rough_width = 60.0f;
        hole->material_zones = {circle_zone(material_zone_type::green, hole->pin_position, 10.0f)};
    }
    return {first, second};
}

// Whether any of `sign` stands where play goes: off the rough or on a tee box.
bool sign_in_play(const play_area& area, const way_sign& sign) {
    std::vector<glm::vec3> spots{sign.post_foot};
    for (const way_board& board : sign.boards) {
        spots.push_back(board.center);
        spots.push_back(board.center + board.points * board.half_width);
        spots.push_back(way_board_tip(sign, board));
    }
    for (const glm::vec3& at : spots) {
        if (sample_area(area, at).material != terrain_material::rough ||
            std::any_of(area.tee_boxes.begin(), area.tee_boxes.end(), [&](const tee_box& box) { return on_tee_box(box, at); })) {
            return true;
        }
    }
    return false;
}

// Every board is square to the way it points, centred on the post's axis line
// with its blunt end at the post, and the boards stack without touching.
void check_built(const way_sign& sign) {
    REQUIRE(!sign.boards.empty());
    for (std::size_t i = 0; i < sign.boards.size(); ++i) {
        const way_board& board = sign.boards[i];
        CHECK(std::abs(glm::dot(board.points, board.face_normal)) < 0.001f);
        const glm::vec3 from_post = horizontal(board.center - sign.post_foot);
        CHECK(near(glm::dot(from_post, board.points), board.half_width, 0.001f));
        CHECK(std::abs(glm::dot(from_post, board.face_normal)) < 0.001f);
        CHECK(std::is_sorted(board.to_holes.begin(), board.to_holes.end()));
        if (i > 0) {
            CHECK(sign.boards[i - 1].center.y - board.center.y >= 2.0f * sign.board_half_height - 0.001f);
        }
    }
}

// How far past the paths' edges `sign`'s post and every board's tip stand.
float sign_past_paths(const play_area& area, const way_sign& sign) {
    float least = distance_past_cart_paths(area.cart_paths, sign.post_foot) - sign.post_radius;
    for (const way_board& board : sign.boards) {
        least = std::min(least, distance_past_cart_paths(area.cart_paths, way_board_tip(sign, board)) - sign.board_thickness * 0.5f);
    }
    return least;
}

// The way signs with `count` boards.
std::vector<const way_sign*> signs_with_boards(const play_area& area, const std::size_t count) {
    std::vector<const way_sign*> found;
    for (const way_sign& sign : area.way_signs) {
        if (sign.boards.size() == count) {
            found.push_back(&sign);
        }
    }
    return found;
}

const glm::vec3 junction_point(-25.0f, 0.0f, 235.0f);

// Three holes, tees 2 and 3 off a junction behind green 1, by different
// branches: from it east to tee 2 (80, 220), and north-west on to tee 3. The
// walk from green 1 comes up beside it from the south and takes the east
// branch; the walk from green 2, whose path runs up along x = 80 and back
// west to the junction, takes the north-west one.
struct two_branch_course {
    std::vector<hole_data> holes;
    play_area area;
};

two_branch_course two_branch_junction() {
    std::vector<hole_data> holes = two_holes();
    holes.push_back(straight_hole(glm::vec3(-90.0f, 0.0f, 305.0f), glm::vec3(-90.0f, 0.0f, 505.0f), 30.0f));
    holes.back().spline.rough_width = 60.0f;
    holes.back().material_zones = {circle_zone(material_zone_type::green, holes.back().pin_position, 10.0f)};
    const course_world_cart_road up{2.5f, {glm::vec3(-25.0f, 0.0f, 150.0f), junction_point}};
    const course_world_cart_road east{2.5f, {junction_point, glm::vec3(80.0f, 0.0f, 235.0f)}};
    const course_world_cart_road to_green_2{2.5f, {glm::vec3(80.0f, 0.0f, 235.0f), glm::vec3(80.0f, 0.0f, 20.0f)}};
    const course_world_cart_road north_west{2.5f, {junction_point, glm::vec3(-90.0f, 0.0f, 300.0f)}};
    return two_branch_course{holes, area_with_roads(holes, {up, east, to_green_2, north_west})};
}
}

TEST_CASE("a sign behind each green points straight at the next tee, path or none, its face back at the green") {
    const std::vector<hole_data> holes = two_holes();
    // Up beside hole 1, past its green, and across to the second tee.
    const course_world_cart_road road{2.5f, {glm::vec3(-25.0f, 0.0f, 10.0f), glm::vec3(-25.0f, 0.0f, 235.0f),
                                             glm::vec3(80.0f, 0.0f, 235.0f)}};
    for (const std::vector<course_world_cart_road>& roads : {std::vector<course_world_cart_road>{road}, std::vector<course_world_cart_road>{}}) {
        const play_area area = area_with_roads(holes, roads);
        REQUIRE(area.way_signs.size() == 1U);
        const way_sign& sign = area.way_signs.front();
        REQUIRE(sign.boards.size() == 1U);
        const way_board& board = sign.boards.front();
        CHECK(board.to_holes == std::vector<std::size_t>{1U});
        // Behind the green (round z = 200, radius 10), from the tee at the origin.
        CHECK(sign.post_foot.z > 210.0f);
        CHECK(!sign_in_play(area, sign));
        const float from_pin = horizontal_distance(sign.post_foot, holes[0].pin_position);
        CHECK(from_pin < 10.0f + tuning().way_sign.green_offset + tuning().way_sign.behind_reach + 1.0f);
        const glm::vec3 to_tee = safe_normalize(horizontal(holes[1].tee_position - sign.post_foot), glm::vec3(1.0f, 0.0f, 0.0f));
        CHECK(glm::dot(board.points, to_tee) > 0.99f);
        CHECK(glm::dot(board.face_normal, horizontal(holes[0].pin_position - sign.post_foot)) > 0.0f);
        check_built(sign);
        // Its post and board stop the ball.
        const std::size_t at_foot = static_cast<std::size_t>(std::count_if(area.solids.begin(), area.solids.end(), [&](const solid& thing) {
            return thing.stops_ball && horizontal_distance(thing.base, sign.post_foot) < 1.0f;
        }));
        CHECK(at_foot >= 2U);
    }
}

TEST_CASE("with the next tee back across the hole, the sign still stands behind the green, off the fairway") {
    std::vector<hole_data> holes = two_holes();
    // The next tee level with hole 1's fairway, off to its right: the
    // straight way there from the green runs down the fairway.
    holes[1] = straight_hole(glm::vec3(60.0f, 0.0f, 150.0f), glm::vec3(200.0f, 0.0f, 150.0f), 30.0f);
    holes[1].spline.rough_width = 60.0f;
    holes[1].material_zones = {circle_zone(material_zone_type::green, holes[1].pin_position, 10.0f)};
    const play_area area = area_with_roads(holes, {});
    REQUIRE(area.way_signs.size() == 1U);
    const way_sign& sign = area.way_signs.front();
    CHECK(sign.post_foot.z > 210.0f);
    CHECK(!sign_in_play(area, sign));
    const glm::vec3 to_tee = safe_normalize(horizontal(holes[1].tee_position - sign.post_foot), glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(glm::dot(sign.boards.front().points, to_tee) > 0.99f);
    check_built(sign);
}

TEST_CASE("with the next tee just behind the green, the sign stands well away from it") {
    std::vector<hole_data> holes = two_holes();
    // The next tee 12 past the green's edge, straight behind it.
    holes[1] = straight_hole(glm::vec3(0.0f, 0.0f, 222.0f), glm::vec3(0.0f, 0.0f, 420.0f), 30.0f);
    holes[1].spline.rough_width = 60.0f;
    holes[1].material_zones = {circle_zone(material_zone_type::green, holes[1].pin_position, 10.0f)};
    const play_area area = area_with_roads(holes, {});
    REQUIRE(area.way_signs.size() == 1U);
    const way_sign& sign = area.way_signs.front();
    for (const tee_box& box : area.tee_boxes) {
        CHECK(distance_from_tee_box(box, sign.post_foot) >= tuning().way_sign.tee_clearance);
    }
    CHECK(!sign_in_play(area, sign));
    check_built(sign);
}

TEST_CASE("at a junction the walks from the greens to the next tees leave by different branches, one post has a board for each, and none points back") {
    const two_branch_course course = two_branch_junction();
    const play_area& area = course.area;
    // A sign behind each of the first two greens, and the one junction's.
    REQUIRE(area.way_signs.size() == 3U);
    const std::vector<const way_sign*> junctions = signs_with_boards(area, 2U);
    REQUIRE(junctions.size() == 1U);
    const way_sign& sign = *junctions.front();
    check_built(sign);
    // Lowest first tee on top: tee 2 east, then tee 3 north-west.
    CHECK(sign.boards[0].to_holes == std::vector<std::size_t>{1U});
    CHECK(sign.boards[1].to_holes == std::vector<std::size_t>{2U});
    CHECK(glm::dot(sign.boards[0].points, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
    const glm::vec3 north_west = safe_normalize(glm::vec3(-65.0f, 0.0f, 65.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    CHECK(glm::dot(sign.boards[1].points, north_west) > 0.99f);
    // The way back down towards green 1 has no board.
    for (const way_board& board : sign.boards) {
        CHECK(glm::dot(board.points, glm::vec3(0.0f, 0.0f, -1.0f)) < 0.5f);
    }
    // Beside the junction, out of play and clear of every path, tips and all.
    CHECK(horizontal_distance(sign.post_foot, junction_point) < 16.0f);
    CHECK(!sign_in_play(area, sign));
    CHECK(sign_past_paths(area, sign) >= tuning().cart_path.clearance - 0.001f);
    CHECK(near(sign.boards[0].center.y - sign.boards[1].center.y, tuning().way_sign.board_height + tuning().way_sign.board_gap, 0.001f));
    CHECK(near(way_sign_top(sign), sign.boards[0].center.y + sign.board_half_height, 0.001f));
    // Each board adds its own wall from the post to its tip, and one pole stands for the lot.
    const auto stopping_at_post = [&](const solid_kind kind) {
        return std::count_if(area.solids.begin(), area.solids.end(), [&](const solid& thing) {
            return thing.kind == kind && thing.stops_ball && horizontal_distance(thing.base, sign.post_foot) < 0.01f;
        });
    };
    CHECK(stopping_at_post(solid_kind::board) == 2);
    CHECK(stopping_at_post(solid_kind::pole) == 1);
}

TEST_CASE("two walks that leave a junction by the same branch share its board, listing both tees") {
    std::vector<hole_data> holes = two_holes();
    // Green 2 in off the junction's north-west spur; tee 3 on past tee 2 to the east.
    holes[1] = straight_hole(glm::vec3(80.0f, 0.0f, 220.0f), glm::vec3(-120.0f, 0.0f, 330.0f), 30.0f);
    holes[1].spline.rough_width = 60.0f;
    holes[1].material_zones = {circle_zone(material_zone_type::green, holes[1].pin_position, 10.0f)};
    holes.push_back(straight_hole(glm::vec3(120.0f, 0.0f, 220.0f), glm::vec3(120.0f, 0.0f, 20.0f), 30.0f));
    holes.back().spline.rough_width = 60.0f;
    holes.back().material_zones = {circle_zone(material_zone_type::green, holes.back().pin_position, 10.0f)};
    const course_world_cart_road up{2.5f, {glm::vec3(-25.0f, 0.0f, 150.0f), junction_point}};
    const course_world_cart_road east{2.5f, {junction_point, glm::vec3(80.0f, 0.0f, 235.0f), glm::vec3(120.0f, 0.0f, 235.0f)}};
    const course_world_cart_road north_west{2.5f, {junction_point, glm::vec3(-90.0f, 0.0f, 300.0f)}};
    const play_area area = area_with_roads(holes, {up, east, north_west});
    const std::vector<const way_sign*> junctions = signs_with_boards(area, 1U);
    const auto shared = std::find_if(junctions.begin(), junctions.end(), [](const way_sign* sign) {
        return sign->boards.front().to_holes.size() == 2U;
    });
    REQUIRE(shared != junctions.end());
    const way_sign& sign = **shared;
    CHECK((sign.boards.front().to_holes == std::vector<std::size_t>{1U, 2U}));
    CHECK(glm::dot(sign.boards.front().points, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
    CHECK(horizontal_distance(sign.post_foot, junction_point) < 16.0f);
    check_built(sign);
}

TEST_CASE("a junction on the fairway gets no sign") {
    const std::vector<hole_data> holes = two_holes();
    // Down hole 1's fairway from by its green to a junction at z = 100, then
    // across to hole 2 and up to its tee; the other branch on down the fairway.
    const glm::vec3 junction(0.0f, 0.0f, 100.0f);
    const course_world_cart_road down{2.5f, {glm::vec3(0.0f, 0.0f, 160.0f), junction}};
    const course_world_cart_road across{2.5f, {junction, glm::vec3(80.0f, 0.0f, 100.0f), glm::vec3(80.0f, 0.0f, 200.0f)}};
    const course_world_cart_road on{2.5f, {junction, glm::vec3(0.0f, 0.0f, 40.0f)}};
    const play_area area = area_with_roads(holes, {down, across, on});
    REQUIRE(area.way_signs.size() == 1U);
    CHECK(area.way_signs.front().post_foot.z > 210.0f);
}

namespace {
// How the junction's sign looks from `eye` at `target`.
glm::mat4 sign_view(const glm::vec3& eye, const glm::vec3& target) {
    return glm::perspective(glm::radians(50.0f), 16.0f / 9.0f, 0.1f, 400.0f) * glm::lookAt(eye, target, world_up);
}
}

TEST_CASE("seen from the path coming in, each board's tip is on its branch's side, and every board reads left to right from both faces") {
    const two_branch_course course = two_branch_junction();
    const std::vector<const way_sign*> junctions = signs_with_boards(course.area, 2U);
    REQUIRE(junctions.size() == 1U);
    const way_sign& sign = *junctions.front();
    const glm::vec3 post_top = sign.post_foot + glm::vec3(0.0f, way_sign_top(sign) - sign.post_foot.y, 0.0f);

    // On the path up from the south, looking north at the post: east is to the
    // left on screen and north-west to the right.
    const glm::vec3 eye = junction_point + glm::vec3(0.0f, 1.6f, -12.0f);
    const glm::mat4 coming_in = sign_view(eye, post_top);
    const std::optional<glm::vec2> post = project_to_screen(coming_in, post_top);
    REQUIRE(post);
    const std::optional<glm::vec2> east_tip = project_to_screen(coming_in, way_board_tip(sign, sign.boards[0]));
    const std::optional<glm::vec2> west_tip = project_to_screen(coming_in, way_board_tip(sign, sign.boards[1]));
    REQUIRE(east_tip);
    REQUIRE(west_tip);
    CHECK(east_tip->x < post->x);
    CHECK(west_tip->x > post->x);

    // Each face, seen square on from its own side: facing the viewer, and its
    // texture's right edge on the viewer's right.
    for (std::size_t i = 0; i < sign.boards.size(); ++i) {
        const way_board& board = sign.boards[i];
        std::vector<hole_sign_vertex> vertices;
        append_way_board_model(vertices, sign, i, test_props().at("hole_sign"));
        for (const float side : {1.0f, -1.0f}) {
            const glm::vec3 normal = board.face_normal * side;
            const glm::vec3 seen_from = board.center + normal * 2.0f;
            const glm::mat4 view = sign_view(seen_from, board.center);
            std::size_t faces = 0;
            float left_x = 0.0f;
            float right_x = 0.0f;
            std::size_t left_count = 0;
            std::size_t right_count = 0;
            for (std::size_t v = 0; v + 2 < vertices.size(); v += 3) {
                if (vertices[v].textured < 0.5f || glm::dot(vertices[v].normal, normal) < 0.99f) {
                    continue;
                }
                ++faces;
                std::array<glm::vec2, 3> corners{};
                for (std::size_t c = 0; c < 3; ++c) {
                    const std::optional<glm::vec2> at = project_to_screen(view, vertices[v + c].position);
                    REQUIRE(at);
                    corners[c] = *at;
                    (vertices[v + c].uv.x > 0.5f ? right_x : left_x) += at->x;
                    ++(vertices[v + c].uv.x > 0.5f ? right_count : left_count);
                }
                // Counter-clockwise on screen: the face the viewer sees, not its back.
                const glm::vec2 ab = corners[1] - corners[0];
                const glm::vec2 ac = corners[2] - corners[0];
                CHECK(ab.x * ac.y - ab.y * ac.x > 0.0f);
            }
            CHECK(faces == 2U);
            REQUIRE(left_count > 0U);
            REQUIRE(right_count > 0U);
            CHECK(right_x / static_cast<float>(right_count) > left_x / static_cast<float>(left_count));
        }
    }
}

namespace {
// How far past the nearest cart path's edge the closest of `thing` stands.
float solid_past_paths(const play_area& area, const solid& thing) {
    if (thing.shape == solid_shape::wall) {
        float least = std::numeric_limits<float>::max();
        constexpr int samples = 16;
        for (int k = 0; k <= samples; ++k) {
            const glm::vec3 at = glm::mix(thing.base, thing.end, static_cast<float>(k) / static_cast<float>(samples));
            least = std::min(least, distance_past_cart_paths(area.cart_paths, at) - thing.radius);
        }
        return least;
    }
    if (thing.shape == solid_shape::box) {
        // Round its outline: a long box (the parked bus) reaches less far sideways than its half diagonal.
        const glm::vec3 across = yaw_left(thing.along);
        float least = std::numeric_limits<float>::max();
        constexpr int samples = 16;
        for (int k = 0; k <= samples; ++k) {
            const float t = static_cast<float>(k) / static_cast<float>(samples) * 2.0f - 1.0f;
            for (const glm::vec3& edge : {thing.along * (thing.half_length * t) + across * thing.half_width,
                                          thing.along * (thing.half_length * t) - across * thing.half_width,
                                          thing.along * thing.half_length + across * (thing.half_width * t),
                                          thing.along * -thing.half_length + across * (thing.half_width * t)}) {
                least = std::min(least, distance_past_cart_paths(area.cart_paths, thing.base + edge));
            }
        }
        return least;
    }
    return distance_past_cart_paths(area.cart_paths, thing.base) - thing.radius;
}
}

TEST_CASE("trees and fences standing within a cart path's clearance are left out; past it they stand") {
    hole_data hole = path_hole();
    const float clearance = tuning().cart_path.clearance;
    const float edge = across_road.width * 0.5f;
    tree_instance tree;
    tree.shape = tree_shape{0.4f, 4.0f, 3.0f, 6.0f};
    tree.position = glm::vec3(-10.0f, 0.0f, 100.0f + edge + 0.2f);  // its trunk over the path's edge
    hole.trees.push_back(tree);
    tree.position = glm::vec3(-5.0f, 0.0f, 100.0f + edge + tree.shape.trunk_radius + clearance + 0.3f);
    hole.trees.push_back(tree);
    course_world_definition world;
    world.hole_starts = {course_world_hole_start{0, hole.tee_position, 0.0f, 0.0f}};
    world.cart_roads = {across_road};
    // A fence across the path, and one along it, clear.
    world.fences = {course_world_fence{{glm::vec3(10.0f, 0.0f, 90.0f), glm::vec3(10.0f, 0.0f, 96.0f),
                                        glm::vec3(10.0f, 0.0f, 104.0f), glm::vec3(10.0f, 0.0f, 110.0f)},
                                       1.5f},
                    course_world_fence{{glm::vec3(-15.0f, 0.0f, 95.0f), glm::vec3(0.0f, 0.0f, 95.0f)}, 1.5f}};
    const play_area area = build_course_area({hole}, world, tuning());
    REQUIRE(area.trees.size() == 1U);
    CHECK(area.trees.front().position.x == -5.0f);
    // The crossing fence opens where it meets the path: no net across it.
    CHECK(area.fences.size() == 3U);
    for (const solid& thing : area.solids) {
        if (thing.stops_cart && !thing.walkable) {
            CHECK(solid_past_paths(area, thing) >= clearance - 0.001f);
        }
    }
}

TEST_CASE("nothing a cart stops at stands within a cart path's clearance on any shipped course") {
    std::vector<std::string> in_lane;
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const float clearance = state.tuning.cart_path.clearance;
        for (const solid& thing : state.area.solids) {
            if (thing.stops_cart && !thing.walkable && solid_past_paths(state.area, thing) < clearance - 0.001f) {
                in_lane.push_back(course.id + " kind " + std::to_string(static_cast<int>(thing.kind)) + " at " +
                                  std::to_string(thing.base.x) + ", " + std::to_string(thing.base.z));
            }
        }
    }
    for (const std::string& where : in_lane) {
        std::cout << "  in a cart path's clearance: " << where << '\n';
    }
    CHECK(in_lane.empty());
}

TEST_CASE("a cart path is drawn on its top as played, facing up, with no gap where stretches meet or turn back") {
    hole_data hole = path_hole();
    hole.pin_position.y = 30.0f;
    hole.spline.control_points = {hole.tee_position, hole.pin_position};
    // Two stretches meeting at an angle, the second doubling back on itself.
    const course_world_cart_road bend{2.5f, {glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f, 0.0f, 140.0f),
                                             glm::vec3(1.0f, 0.0f, 110.0f)}};
    const play_area area = area_with_roads({hole}, {across_road, bend});
    const render_static_mesh drawn = with_cart_paths(render_static_mesh{}, area, glm::vec3(0.4f), ground_light_bake{});
    REQUIRE(!drawn.indices.empty());
    CHECK(drawn.indices.size() % 3U == 0U);
    CHECK(drawn.bounds.valid);
    for (const render_terrain_vertex& vertex : drawn.vertices) {
        CHECK(near(vertex.position.y, sample_area(area, vertex.position).point.y, 0.0001f));
        CHECK(vertex.paved() == 1.0f);
    }
    for (std::size_t i = 0; i < drawn.indices.size(); i += 3) {
        const glm::vec3 a = drawn.vertices[drawn.indices[i]].position;
        const glm::vec3 b = drawn.vertices[drawn.indices[i + 1]].position;
        const glm::vec3 c = drawn.vertices[drawn.indices[i + 2]].position;
        CHECK(glm::cross(b - a, c - a).y >= 0.0f);
    }
    // Every point of the paths' footprint is covered by a drawn triangle.
    const auto covered = [&](const glm::vec3& at) {
        for (std::size_t i = 0; i < drawn.indices.size(); i += 3) {
            const glm::vec2 p(at.x, at.z);
            const glm::vec3 a = drawn.vertices[drawn.indices[i]].position;
            const glm::vec3 b = drawn.vertices[drawn.indices[i + 1]].position;
            const glm::vec3 c = drawn.vertices[drawn.indices[i + 2]].position;
            const auto side = [](const glm::vec2& u, const glm::vec2& v, const glm::vec2& w) {
                return (v.x - u.x) * (w.y - u.y) - (v.y - u.y) * (w.x - u.x);
            };
            const glm::vec2 a2(a.x, a.z), b2(b.x, b.z), c2(c.x, c.z);
            const float s1 = side(a2, b2, p), s2 = side(b2, c2, p), s3 = side(c2, a2, p);
            if ((s1 >= -1e-4f && s2 >= -1e-4f && s3 >= -1e-4f) || (s1 <= 1e-4f && s2 <= 1e-4f && s3 <= 1e-4f)) {
                return true;
            }
        }
        return false;
    };
    // The junction, the hairpin and either side of the bend's joints.
    for (const glm::vec3& at : {glm::vec3(0.0f, 0.0f, 100.0f), glm::vec3(0.0f, 0.0f, 140.0f), glm::vec3(0.0f, 0.0f, 141.0f),
                                glm::vec3(0.9f, 0.0f, 139.0f), glm::vec3(-1.0f, 0.0f, 101.0f), glm::vec3(0.8f, 0.0f, 99.0f)}) {
        CHECK(covered(at));
    }
}

namespace {
// A green sign: one board for one tee, pointing straight at it, on a post
// near the green before it.
template <typename holes_t>
bool is_green_sign(const way_sign& sign, const holes_t& holes) {
    if (sign.boards.size() != 1U || sign.boards.front().to_holes.size() != 1U) {
        return false;
    }
    const std::size_t to = sign.boards.front().to_holes.front();
    return to >= 1U && to < holes.size() && horizontal_distance(sign.post_foot, holes[to - 1].pin_position) < 100.0f &&
        glm::dot(sign.boards.front().points,
                 safe_normalize(horizontal(holes[to].tee_position - sign.post_foot), glm::vec3(1.0f, 0.0f, 0.0f))) > 0.9999f;
}

// What the walks from each green to the next tee say about the junctions:
// for each junction node, its branches' directions and the tees each leads to.
struct expected_board {
    glm::vec3 junction{0.0f};
    glm::vec3 points{0.0f};
    std::vector<std::size_t> to_holes;
};

template <typename holes_t>
std::vector<expected_board> expected_boards(const play_area& area, const holes_t& holes, const float path_reach) {
    std::vector<expected_board> expected;
    const path_graph graph = graph_of(area.cart_paths);
    if (graph.nodes.empty()) {
        return expected;
    }
    const auto near_path = [&](const glm::vec3& point) -> std::optional<std::size_t> {
        const std::size_t node = nearest_node(graph, point);
        return horizontal_distance(graph.nodes[node], point) <= path_reach ? std::optional<std::size_t>(node) : std::nullopt;
    };
    for (std::size_t i = 0; i + 1 < holes.size(); ++i) {
        const std::optional<std::size_t> from = near_path(holes[i].pin_position);
        const std::optional<std::size_t> to = near_path(holes[i + 1].tee_position);
        const auto route = from && to ? route_along(graph, *from, *to) : std::nullopt;
        for (std::size_t k = 0; route && k + 1 < route->size(); ++k) {
            if (graph.links[(*route)[k]].size() < 3) {
                continue;
            }
            const glm::vec3& at = graph.nodes[(*route)[k]];
            const glm::vec3 points = safe_normalize(horizontal(graph.nodes[(*route)[k + 1]] - at), glm::vec3(0.0f, 0.0f, 1.0f));
            const auto same = std::find_if(expected.begin(), expected.end(), [&](const expected_board& other) {
                return other.junction == at && glm::dot(other.points, points) > 0.9999f;
            });
            if (same == expected.end()) {
                expected.push_back(expected_board{at, points, {i + 1}});
            } else {
                same->to_holes.push_back(i + 1);
            }
        }
    }
    return expected;
}
}

TEST_CASE("every shipped course has a sign behind each green but the last, junction boards only along branches with tees, and no way sign stands in play or by a tee") {
    std::vector<std::string> unsigned_greens;
    std::vector<std::string> in_play;
    std::vector<std::string> by_tee;
    std::vector<std::string> unfounded;
    std::vector<std::string> too_close;
    const text_assets& text = shipped_text_assets();
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        for (std::size_t i = 0; i + 1 < state.holes.size(); ++i) {
            const glm::vec3& pin = state.holes[i].pin_position;
            const auto tee_sign = std::find_if(state.area.signs.begin(), state.area.signs.end(),
                                               [&](const hole_sign& sign) { return sign.area_hole == i; });
            REQUIRE(tee_sign != state.area.signs.end());
            const std::vector<glm::vec3>& line = tee_sign->line_of_play;
            const glm::vec3 behind = horizontal(pin - line[line.size() - 2]);
            const bool signed_way = std::any_of(state.area.way_signs.begin(), state.area.way_signs.end(), [&](const way_sign& sign) {
                return is_green_sign(sign, state.holes) && sign.boards.front().to_holes.front() == i + 1 &&
                    glm::dot(horizontal(sign.post_foot - pin), behind) > 0.0f;
            });
            if (!signed_way) {
                unsigned_greens.push_back(course.id + " green " + std::to_string(i + 1));
            }
        }
        // One green sign for each green but the last, and no board for tee 1 (the bus's walk) or past the last.
        const std::vector<expected_board> expected =
            expected_boards(state.area, state.holes, state.tuning.way_sign.path_reach);
        std::size_t green_signs = 0;
        for (const way_sign& sign : state.area.way_signs) {
            const std::string name = course.id + " sign by (" + std::to_string(sign.post_foot.x) + ", " + std::to_string(sign.post_foot.z) + ")";
            green_signs += is_green_sign(sign, state.holes) ? 1U : 0U;
            for (const way_board& board : sign.boards) {
                CHECK(std::abs(glm::dot(board.points, board.face_normal)) < 0.001f);
                CHECK(!board.to_holes.empty());
                CHECK(std::is_sorted(board.to_holes.begin(), board.to_holes.end()));
                for (const std::size_t to : board.to_holes) {
                    CHECK(to >= 1U);
                    CHECK(to < state.holes.size());
                }
                if (!is_green_sign(sign, state.holes) &&
                    std::none_of(expected.begin(), expected.end(), [&](const expected_board& want) {
                        return want.to_holes == board.to_holes && glm::dot(want.points, board.points) > 0.9999f &&
                            horizontal_distance(want.junction, sign.post_foot) < 20.0f;
                    })) {
                    unfounded.push_back(name + " board to " + way_sign_label(board.to_holes));
                }
            }
            if (!is_green_sign(sign, state.holes) && sign_past_paths(state.area, sign) < state.tuning.cart_path.clearance - 0.001f) {
                too_close.push_back(name);
            }
            if (sign_in_play(state.area, sign)) {
                in_play.push_back(name);
            }
            for (const tee_box& box : state.area.tee_boxes) {
                if (distance_from_tee_box(box, sign.post_foot) < state.tuning.way_sign.tee_clearance) {
                    by_tee.push_back(name + " by tee " + std::to_string(box.hole + 1));
                }
            }
            // Every face fits its board at the one text scale.
            for (const way_board& board : sign.boards) {
                const int width = way_sign_face_width(board.half_width, sign.board_half_height);
                const text_layout layout = layout_way_sign_label(text, way_sign_label(board.to_holes), width);
                CHECK(!layout.truncated);
                CHECK(layout.scale == find_text_style(text, style_way_sign_number).min_scale);
                // Its wall runs the length of its box, from the post's side to where its tip starts.
                const glm::vec3 foot = board.center - glm::vec3(0.0f, sign.board_half_height, 0.0f);
                CHECK(std::any_of(state.area.solids.begin(), state.area.solids.end(), [&](const solid& thing) {
                    return thing.kind == solid_kind::board && horizontal_distance(thing.base, foot - board.points * board.half_width) < 0.001f &&
                        horizontal_distance(thing.end, foot + board.points * board.half_width) < 0.001f;
                }));
                overlay_batch batch;
                draw_way_sign_face(batch, text, way_sign_label(board.to_holes), width);
                CHECK(batch.truncated_text_count == 0U);
            }
        }
        CHECK(green_signs == state.holes.size() - 1);
    }
    for (const std::string& green : unsigned_greens) {
        std::cout << "  no way sign behind: " << green << '\n';
    }
    for (const std::string& sign : in_play) {
        std::cout << "  in play: " << sign << '\n';
    }
    for (const std::string& sign : by_tee) {
        std::cout << "  by a tee: " << sign << '\n';
    }
    for (const std::string& sign : unfounded) {
        std::cout << "  points along a branch with no such tee: " << sign << '\n';
    }
    for (const std::string& sign : too_close) {
        std::cout << "  too near a path: " << sign << '\n';
    }
    CHECK(unsigned_greens.empty());
    CHECK(in_play.empty());
    CHECK(by_tee.empty());
    CHECK(unfounded.empty());
    CHECK(too_close.empty());
}

TEST_CASE("every shipped course's drawn cart paths lie on their walkable top, nowhere under the ground") {
    std::vector<std::string> sunk;
    for (const course_definition& course : shipped_content().courses) {
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const render_static_mesh drawn = with_cart_paths(render_static_mesh{}, state.area, glm::vec3(0.4f), ground_light_bake{});
        std::size_t off_top = 0;
        for (const render_terrain_vertex& vertex : drawn.vertices) {
            const terrain_sample top = sample_area(state.area, vertex.position);
            off_top += top.top_solid < 0 || std::abs(vertex.position.y - top.point.y) > 0.001f ? 1U : 0U;
        }
        if (off_top > 0) {
            sunk.push_back(course.id + ": " + std::to_string(off_top) + " vertices off the walkable top");
        }
        std::size_t under = 0;
        for (std::size_t i = 0; i < drawn.indices.size(); i += 3) {
            const glm::vec3 middle = (drawn.vertices[drawn.indices[i]].position + drawn.vertices[drawn.indices[i + 1]].position +
                                      drawn.vertices[drawn.indices[i + 2]].position) / 3.0f;
            // The ground must never show through the triangles' interiors.
            const float ground = sample_terrain_mesh(state.area.ground, middle, state.area.ground_y).point.y;
            under += middle.y < ground - 0.002f ? 1U : 0U;
        }
        if (under > 0) {
            sunk.push_back(course.id + ": " + std::to_string(under) + " of " + std::to_string(drawn.indices.size() / 3U));
        }
    }
    for (const std::string& course : sunk) {
        std::cout << "  drawn path under the ground: " << course << '\n';
    }
    CHECK(sunk.empty());
}
