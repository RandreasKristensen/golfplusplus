#include "doctest.h"

#include "game/area_solids.h"
#include "game/cart_path.h"
#include "game/game_state.h"
#include "game/hole_sign.h"
#include "game/movement_step.h"
#include "game/play_area.h"
#include "game/shot_simulation.h"
#include "physics/solid.h"
#include "physics/vector_math.h"
#include "renderer/cart_path_mesh.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include <string>
#include <vector>

#include <glm/geometric.hpp>

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
    CHECK(cart_ground_at(area, tuning(), 0.0f, glm::vec3(30.0f, 0.0f, 100.0f)).water_depth == 0.0f);
    CHECK(cart_ground_at(area, tuning(), 0.0f, glm::vec3(30.0f, 0.0f, 104.0f)).water_depth > 0.0f);
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

TEST_CASE("a sign by each green points along the path to the next tee, beside the path, its face towards it") {
    hole_data first = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 30.0f);
    hole_data second = straight_hole(glm::vec3(80.0f, 0.0f, 220.0f), glm::vec3(80.0f, 0.0f, 20.0f), 30.0f);
    for (hole_data* hole : {&first, &second}) {
        hole->spline.rough_width = 60.0f;
        hole->material_zones = {circle_zone(material_zone_type::green, hole->pin_position, 10.0f)};
    }
    // Up beside hole 1, past its green, and across to the second tee.
    const course_world_cart_road road{2.5f, {glm::vec3(-25.0f, 0.0f, 10.0f), glm::vec3(-25.0f, 0.0f, 235.0f),
                                             glm::vec3(80.0f, 0.0f, 235.0f)}};
    const play_area area = area_with_roads({first, second}, {road});
    REQUIRE(area.way_signs.size() == 1U);
    const way_sign& sign = area.way_signs.front();
    CHECK(sign.to_hole == 1U);
    // Hole 1's green is at z = 200, the path passing it at x = -25 going on
    // to tee 2: up the path (+z), away from where it came.
    CHECK(glm::dot(sign.points, glm::vec3(0.0f, 0.0f, 1.0f)) > 0.9f);
    // Beside the path, on its side away from the pin, its face looking back over it.
    CHECK(sign.post_foot.x < -25.0f - road.width * 0.5f);
    CHECK(path_under(area, sign.post_foot) == nullptr);
    CHECK(glm::dot(sign.face_normal, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.99f);
    // Its board is fixed to the post's front, so the post never crosses its face.
    CHECK(glm::dot(horizontal(sign.board_center - sign.post_foot), sign.face_normal) >=
          sign.board_thickness * 0.5f + sign.post_radius - 0.001f);
    CHECK(glm::length(horizontal(sign.board_center - sign.post_foot) -
                      sign.face_normal * glm::dot(horizontal(sign.board_center - sign.post_foot), sign.face_normal)) < 0.001f);
    // Its post and board stop the ball.
    const std::size_t at_foot = static_cast<std::size_t>(std::count_if(area.solids.begin(), area.solids.end(), [&](const solid& thing) {
        return thing.stops_ball && horizontal_distance(thing.base, sign.post_foot) < 1.0f;
    }));
    CHECK(at_foot >= 2U);
}

TEST_CASE("with no path that way, the sign stands just off the green pointing straight at the next tee") {
    hole_data first = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 30.0f);
    hole_data second = straight_hole(glm::vec3(80.0f, 0.0f, 220.0f), glm::vec3(80.0f, 0.0f, 20.0f), 30.0f);
    for (hole_data* hole : {&first, &second}) {
        hole->spline.rough_width = 60.0f;
        hole->material_zones = {circle_zone(material_zone_type::green, hole->pin_position, 10.0f)};
    }
    const play_area area = area_with_roads({first, second}, {});
    REQUIRE(area.way_signs.size() == 1U);
    const way_sign& sign = area.way_signs.front();
    CHECK(sign.to_hole == 1U);
    const glm::vec3 to_tee = safe_normalize(horizontal(second.tee_position - sign.board_center), glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(glm::dot(sign.points, to_tee) > 0.99f);
    // Off the green, a few metres past its edge.
    CHECK(sample_area(area, sign.post_foot).material != terrain_material::green);
    const float from_pin = horizontal_distance(sign.post_foot, first.pin_position);
    CHECK(from_pin > 10.0f);
    CHECK(from_pin < 10.0f + tuning().way_sign.green_offset + tuning().way_sign.path_offset + 4.0f);
    // Its face looks across the straight way, where you walk past it.
    CHECK(std::abs(glm::dot(sign.face_normal, sign.points)) < 0.01f);
}

TEST_CASE("along a path with a junction on the way, a second sign there points the way to take") {
    hole_data first = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 30.0f);
    hole_data second = straight_hole(glm::vec3(80.0f, 0.0f, 220.0f), glm::vec3(80.0f, 0.0f, 20.0f), 30.0f);
    for (hole_data* hole : {&first, &second}) {
        hole->spline.rough_width = 60.0f;
        hole->material_zones = {circle_zone(material_zone_type::green, hole->pin_position, 10.0f)};
    }
    // Up beside hole 1 to a junction behind its green: on to tee 2 (east),
    // or off north-west elsewhere.
    const glm::vec3 junction(-25.0f, 0.0f, 235.0f);
    const course_world_cart_road up{2.5f, {glm::vec3(-25.0f, 0.0f, 150.0f), junction}};
    const course_world_cart_road on{2.5f, {junction, glm::vec3(80.0f, 0.0f, 235.0f)}};
    const course_world_cart_road off{2.5f, {junction, glm::vec3(-90.0f, 0.0f, 300.0f)}};
    const play_area area = area_with_roads({first, second}, {up, on, off});
    REQUIRE(area.way_signs.size() == 2U);
    const way_sign& at_junction = area.way_signs.back();
    CHECK(at_junction.to_hole == 1U);
    // Just before it on the way in, beside the path up.
    CHECK(at_junction.post_foot.z < junction.z);
    CHECK(horizontal_distance(at_junction.post_foot, junction) < tuning().way_sign.junction_lead + 4.0f);
    CHECK(glm::dot(at_junction.points, glm::vec3(1.0f, 0.0f, 0.0f)) > 0.95f);
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
    return distance_past_cart_paths(area.cart_paths, thing.base) -
        (thing.shape == solid_shape::box ? std::hypot(thing.half_length, thing.half_width) : thing.radius);
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
        CHECK(vertex.paved == 1.0f);
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

TEST_CASE("every shipped course has a sign by each green but the last pointing the way to the next tee") {
    std::vector<std::string> unsigned_greens;
    for (const course_definition& course : shipped_content().courses) {
        if (course.practice) {
            continue;
        }
        game_state state = make_game_state(shipped_content(), save_data{});
        REQUIRE(start_course(state, course));
        const float reach = state.tuning.way_sign.green_reach + state.tuning.way_sign.path_offset + 4.0f;
        for (std::size_t i = 0; i + 1 < state.holes.size(); ++i) {
            const bool signed_way = std::any_of(state.area.way_signs.begin(), state.area.way_signs.end(), [&](const way_sign& sign) {
                return sign.to_hole == i + 1 && horizontal_distance(sign.post_foot, state.holes[i].pin_position) <= reach;
            });
            if (!signed_way) {
                unsigned_greens.push_back(course.id + " green " + std::to_string(i + 1));
            }
        }
    }
    for (const std::string& green : unsigned_greens) {
        std::cout << "  no way sign by: " << green << '\n';
    }
    CHECK(unsigned_greens.empty());
}

TEST_CASE("every shipped course's drawn cart paths lie on their walkable top, nowhere under the ground") {
    std::vector<std::string> sunk;
    for (const course_definition& course : shipped_content().courses) {
        if (course.practice) {
            continue;
        }
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
