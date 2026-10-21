#include "doctest.h"

#include "audio/audio_manifest.h"
#include "game/cart_seats.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <string>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
// Stood beside my cart's wheel, where stepping out of it leaves me.
void stand_beside_my_cart(game_state& state) {
    state.player.position = anchor_on_terrain(state.area, step_out_position(state.cart.pose, cart_seat::driver, state.tuning.cart));
}

void play_step(game_state& state) {
    const int frames = static_cast<int>(state.tuning.cart.step_seconds / 0.02f) + 2;
    for (int i = 0; i < frames; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
}

bool has_audio(const game_state& state, const audio_event_type type) {
    return std::any_of(state.audio_events.begin(), state.audio_events.end(),
                       [type](const audio_event& event) { return event.type == type; });
}
}

TEST_CASE("seats sit either side of a cart's middle, the driver's on its left, stepping out further out") {
    const cart_tuning& tuning = shipped_content().tuning.cart;
    const cart_pose cart{glm::vec3(4.0f, 1.0f, -2.0f), 0.7f};
    const glm::vec3 left = yaw_left(yaw_direction(cart.yaw));
    const glm::vec3 driver = seat_position(cart, cart_seat::driver, tuning);
    const glm::vec3 passenger = seat_position(cart, cart_seat::passenger, tuning);
    CHECK(near(glm::dot(driver - cart.position, left), tuning.seat_offset, 0.0001f));
    CHECK(near(glm::dot(passenger - cart.position, left), -tuning.seat_offset, 0.0001f));
    CHECK(near(glm::dot(step_out_position(cart, cart_seat::driver, tuning) - cart.position, left),
               tuning.seat_offset + tuning.step_out_distance, 0.0001f));
    CHECK(near(glm::dot(step_out_position(cart, cart_seat::passenger, tuning) - cart.position, left),
               -tuning.seat_offset - tuning.step_out_distance, 0.0001f));
    // The cart round a seat is the cart that seat is in.
    for (const cart_seat seat : {cart_seat::driver, cart_seat::passenger}) {
        const cart_pose around = cart_around_seat(seat_position(cart, seat, tuning), cart.yaw, seat, tuning);
        CHECK(near(around.position, cart.position, 0.0001f));
    }
    // Stepping out leaves you within reach to step back in.
    CHECK(in_seat_reach(cart, cart_seat::driver, step_out_position(cart, cart_seat::driver, tuning), tuning));
    CHECK(!in_seat_reach(cart, cart_seat::driver, driver + left * (tuning.board_reach + 0.1f), tuning));
}

TEST_CASE("a course starts with my cart parked empty beside hole 1, facing down it, clear of its start") {
    const game_state state = started_game(fixture_hub_course());
    const course_world_hole_start& start = state.hub->world.hole_starts.front();
    const cart_pose parked = parked_cart_pose(start.position, state.hub->markers.front().pin_position, 0, state.tuning.cart);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(parked.position), 0.0001f));
    CHECK(near(state.cart.pose.position.y, terrain_height(state.area, state.cart.pose.position), 0.0001f));
    CHECK(near(state.cart.pose.yaw, yaw_towards(start.position, state.hub->markers.front().pin_position), 0.0001f));
    CHECK(on_foot(state));
    CHECK(state.cart.velocity == 0.0f);
    // Beside the hole, not on its start: the first press there starts hole 1.
    const float apart = horizontal_distance(state.cart.pose.position, start.position);
    CHECK(apart > start.interaction_radius);
    CHECK(apart < 15.0f);
    CHECK(!nearest_seat(state).has_value());
    CHECK(nearby_hole_start(state).has_value());
}

TEST_CASE("walking up to my cart's wheel and interacting steps in over the step, then I drive it") {
    game_state state = started_game(fixture_hub_course());
    stand_beside_my_cart(state);
    const glm::vec3 beside = state.player.position;
    REQUIRE(nearest_seat(state).has_value());

    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.ride.has_value());
    CHECK(state.ride->step.has_value());
    CHECK(!driving_cart(state));
    // Partway: between where I stood and the seat.
    update_game(state, game_input{}, state.tuning.cart.step_seconds * 0.4f);
    const glm::vec3 seat = seat_position(state.cart.pose, cart_seat::driver, state.tuning.cart);
    CHECK(horizontal_distance(state.player.position, beside) > 0.01f);
    CHECK(horizontal_distance(state.player.position, seat) > 0.01f);

    play_step(state);
    CHECK(driving_cart(state));
    CHECK(near(horizontal(state.player.position), horizontal(seat), 0.0001f));
    CHECK(near(state.player.yaw, state.cart.pose.yaw, 0.0001f));
}

TEST_CASE("my cart is in the world whether I walk or sit at its wheel, and its steering wheel follows the steering") {
    game_state state = started_game(fixture_hub_course());
    std::vector<cart_in_world> carts = carts_in_world(state);
    REQUIRE(carts.size() == 1U);
    CHECK(!carts[0].driven);
    stand_beside_my_cart(state);
    update_game(state, interact_input(), 0.02f);
    play_step(state);
    REQUIRE(driving_cart(state));
    carts = carts_in_world(state);
    REQUIRE(carts.size() == 1U);
    CHECK(carts[0].driven);
    CHECK(near(carts[0].pose.position, state.cart.pose.position));

    game_input steer_left;
    steer_left.strafe_left_held = true;
    update_game(state, steer_left, 0.02f);
    const float eased = state.cart.steer;
    CHECK(eased > 0.0f);
    CHECK(eased < 1.0f);
    for (int i = 0; i < 50; ++i) {
        update_game(state, steer_left, 0.02f);
    }
    CHECK(state.cart.steer > 0.95f);
    CHECK(near(carts_in_world(state)[0].steer, state.cart.steer, 0.0001f));
    update_game(state, interact_input(), 0.02f);
    CHECK(state.cart.steer == 0.0f);
}

TEST_CASE("the cart engine starts as I sit down at its wheel") {
    game_state state = started_game(fixture_hub_course());
    stand_beside_my_cart(state);
    update_game(state, interact_input(), 0.02f);
    state.audio_events.clear();
    for (int i = 0; i < 100 && !driving_cart(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(has_audio(state, audio_event_type::cart_start));
}

TEST_CASE("out of reach of the seat, interacting does not step in") {
    game_state state = started_game(fixture_hub_course());
    const cart_tuning& tuning = state.tuning.cart;
    const glm::vec3 seat = seat_position(state.cart.pose, cart_seat::driver, tuning);
    const glm::vec3 out = yaw_left(yaw_direction(state.cart.pose.yaw));
    state.player.position = anchor_on_terrain(state.area, seat + out * (tuning.board_reach + 0.5f));
    CHECK(!nearest_seat(state).has_value());
    update_game(state, interact_input(), 0.02f);
    CHECK(on_foot(state));
}

TEST_CASE("a hole start nearer than the seat starts the hole; a seat nearer than it is stepped into") {
    game_state state = started_game(fixture_hub_course());
    const cart_tuning& tuning = state.tuning.cart;
    const course_world_hole_start& start = state.hub->world.hole_starts.front();
    // The cart parked with its wheel just off the start, within both reaches.
    const glm::vec3 across = glm::vec3(1.0f, 0.0f, 0.0f);
    state.cart.pose = cart_around_seat(start.position + across * 2.0f, 0.0f, cart_seat::driver, tuning);

    game_state at_start = state;
    at_start.player.position = anchor_on_terrain(at_start.area, start.position + across * 0.5f);
    REQUIRE(nearest_seat(at_start).has_value());
    update_game(at_start, interact_input(), 0.02f);
    CHECK(at_start.hole.has_value());
    CHECK(on_foot(at_start));

    game_state at_seat = state;
    at_seat.player.position = anchor_on_terrain(at_seat.area, start.position + across * 1.7f);
    REQUIRE(nearby_hole_start(at_seat).has_value());
    update_game(at_seat, interact_input(), 0.02f);
    CHECK(!at_seat.hole.has_value());
    CHECK(at_seat.ride.has_value());
}

TEST_CASE("the cart stays where I left it when a hole starts and when I come back to the hub") {
    game_state state = started_game(fixture_hub_course());
    stand_beside_my_cart(state);
    update_game(state, interact_input(), 0.02f);
    play_step(state);
    REQUIRE(driving_cart(state));
    game_input drive;
    drive.forward_held = true;
    for (int i = 0; i < 20; ++i) {
        update_game(state, drive, 0.05f);
    }
    update_game(state, interact_input(), 0.02f);
    play_step(state);
    REQUIRE(on_foot(state));
    const cart_pose left_at = state.cart.pose;

    REQUIRE(start_hub_hole(state, 0));
    CHECK(near(state.cart.pose.position, left_at.position));
    abandon_hole(state, state.hub->world.hole_starts.front().position);
    CHECK(near(state.cart.pose.position, left_at.position));
    CHECK(on_foot(state));
}

TEST_CASE("backing up is slower than driving forward, and the cart only turns while it moves") {
    game_state state = started_hole();
    // Sat on the tee, backing into open ground.
    state.area.obstacles = upright_obstacles{};
    state.area.cart_obstacles = upright_obstacles{};
    sit_in_cart(state);
    const float yaw = state.cart.pose.yaw;
    game_input turn;
    turn.strafe_left_held = true;
    update_game(state, turn, 0.05f);
    CHECK(near(state.cart.pose.yaw, yaw));

    game_input back;
    back.back_held = true;
    for (int i = 0; i < 60; ++i) {
        update_game(state, back, 0.05f);
    }
    CHECK(state.cart.velocity < 0.0f);
    CHECK(near(state.cart.velocity, -state.tuning.cart.speed * state.tuning.cart.reverse_speed_scale, 0.01f));
}

TEST_CASE("the cart does not drift: interact at its wheel only steps out, and no skid sound or drifting skill ships") {
    game_state state = started_hole();
    sit_in_cart(state);
    game_input drive;
    drive.forward_held = true;
    for (int i = 0; i < 40; ++i) {
        drive.interact = i == 20;
        update_game(state, drive, 0.05f);
        CHECK(std::abs(state.cart.velocity) <= fastest_cart_speed(state.tuning.cart) + 0.0001f);
    }
    CHECK(on_foot(state));
    CHECK(fastest_cart_speed(state.tuning.cart) == state.tuning.cart.speed * state.tuning.cart.road_speed_scale);

    const audio_manifest_parse_result sounds = load_audio_manifest_from_file(asset_root() + "/audio/sounds.json");
    REQUIRE(sounds.manifest.has_value());
    for (const audio_sound_definition& sound : sounds.manifest->sounds) {
        CHECK(sound.id.find("drift") == std::string::npos);
    }
    for (const skill_definition& skill : shipped_content().skills) {
        CHECK(skill.id.find("drift") == std::string::npos);
    }
}

TEST_CASE("at the wheel A and D steer, while the arrow keys and the mouse turn my head, not the cart") {
    game_state state = started_hole();
    sit_in_cart(state);
    const float yaw = state.cart.pose.yaw;

    game_input look;
    look.turn_left_held = true;
    look.look_yaw = 0.2f;
    look.forward_held = true;
    for (int i = 0; i < 10; ++i) {
        update_game(state, look, 0.05f);
    }
    CHECK(near(state.cart.pose.yaw, yaw));
    CHECK(near(state.look.head_yaw, wrap_angle(10.0f * (state.tuning.player.turn_rate * 0.05f + 0.2f)), 0.0001f));
    CHECK(near(view_yaw(state), wrap_angle(state.cart.pose.yaw + state.look.head_yaw), 0.0001f));
    CHECK(state.cart.velocity > 0.0f);

    game_input steer;
    steer.forward_held = true;
    steer.strafe_left_held = true;
    update_game(state, steer, 0.05f);
    CHECK(state.cart.pose.yaw > yaw);
}

TEST_CASE("stepping out of the cart I go on facing where my head was turned") {
    game_state state = started_hole();
    sit_in_cart(state);
    game_input look;
    look.look_yaw = 0.8f;
    update_game(state, look, 0.02f);
    const float looking = view_yaw(state);

    update_game(state, interact_input(), 0.02f);
    play_step(state);
    REQUIRE(on_foot(state));
    CHECK(near(state.player.yaw, looking, 0.0001f));
    CHECK(state.look.head_yaw == 0.0f);
}

TEST_CASE("trees, poles, signs, nets and flagsticks stand in the way of players and carts, tee boxes of carts only") {
    const game_state state = started_game(fixture_hub_course());
    const play_area& area = state.area;
    CHECK(area.obstacles.posts.size() ==
          area.trees.size() + area.sign_posts.size() + area.fence_poles.size() + area.floodlight_poles.size() + area.holes.size());
    CHECK(area.obstacles.walls.size() == area.signs.size() + area.fence_panels.size());
    CHECK(area.obstacles.blocks.empty());
    CHECK(area.cart_obstacles.blocks.size() == area.tee_boxes.size());
    CHECK(area.cart_obstacles.posts.empty());
}

TEST_CASE("walking into a tree stops me at its trunk, with one bump heard") {
    game_state state = started_game(fixture_hub_course());
    state.area.obstacles = upright_obstacles{};
    state.area.cart_obstacles = upright_obstacles{};
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    const glm::vec3 trunk = state.player.position + ahead * 2.0f;
    state.area.obstacles.posts.push_back(upright_post{glm::vec2(trunk.x, trunk.z), 0.4f});
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 50; ++i) {
        update_game(state, forward, 0.02f);
    }
    CHECK(near(horizontal_distance(state.player.position, trunk), 0.4f + state.tuning.player.body_radius, 0.01f));
    CHECK(std::count_if(state.audio_events.begin(), state.audio_events.end(), [](const audio_event& event) {
              return event.type == audio_event_type::player_bump;
          }) == 1);
    CHECK(!has_audio(state, audio_event_type::cart_crash));
}

TEST_CASE("driving into a tee box crashes the cart: it stops at it, bounces back and is heard") {
    game_state state = started_game(fixture_hub_course());
    state.area.obstacles = upright_obstacles{};
    state.area.cart_obstacles = upright_obstacles{};
    sit_in_cart(state);
    const cart_tuning& tuning = state.tuning.cart;
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    const glm::vec3 box = state.cart.pose.position + ahead * 12.0f;
    state.area.cart_obstacles.blocks.push_back(
        upright_block{glm::vec2(box.x, box.z), glm::vec2(ahead.x, ahead.z), 3.0f, 2.0f});
    game_input forward;
    forward.forward_held = true;
    bool crashed = false;
    for (int i = 0; i < 60 && !crashed; ++i) {
        const float speed = state.cart.velocity;
        update_game(state, forward, 0.02f);
        crashed = has_audio(state, audio_event_type::cart_crash);
        if (crashed) {
            CHECK(speed >= tuning.crash_sound_speed);
            CHECK(state.cart.velocity <= 0.0f);
        }
    }
    CHECK(crashed);
    const float front = glm::dot(state.cart.pose.position - box, ahead) + tuning.body_ahead + tuning.body_radius;
    CHECK(front <= -3.0f + 0.01f);
    CHECK(!has_audio(state, audio_event_type::player_bump));

    // On foot the tee box is no wall.
    game_state walker = started_game(fixture_hub_course());
    walker.area.obstacles = upright_obstacles{};
    walker.area.cart_obstacles = upright_obstacles{};
    const glm::vec3 start = walker.player.position;
    const glm::vec3 path = yaw_direction(walker.player.yaw);
    const glm::vec3 tee = start + path * 3.0f;
    walker.area.cart_obstacles.blocks.push_back(
        upright_block{glm::vec2(tee.x, tee.z), glm::vec2(path.x, path.z), 3.0f, 2.0f});
    game_input walk;
    walk.forward_held = true;
    for (int i = 0; i < 20; ++i) {
        update_game(walker, walk, 0.02f);
    }
    CHECK(horizontal_distance(walker.player.position, start) > 0.9f * walker.tuning.player.walk_speed * 0.4f);
}

TEST_CASE("driving along a fence the cart scrapes nearly to a stop instead of gliding along it") {
    game_state state = started_game(fixture_hub_course());
    state.area.obstacles = upright_obstacles{};
    state.area.cart_obstacles = upright_obstacles{};
    sit_in_cart(state);
    // A net ahead, slanting across the way at 30 degrees.
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    const glm::vec3 slant = yaw_direction(state.cart.pose.yaw + glm::radians(60.0f));
    const glm::vec3 mid = state.cart.pose.position + ahead * 6.0f;
    state.area.obstacles.walls.push_back(upright_wall{glm::vec2(mid.x, mid.z) - glm::vec2(slant.x, slant.z) * 40.0f,
                                                      glm::vec2(mid.x, mid.z) + glm::vec2(slant.x, slant.z) * 40.0f, 0.05f});
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 50; ++i) {
        update_game(state, forward, 0.02f);
    }
    REQUIRE(state.cart.against_obstacle);
    // Throttle held, it barely creeps along the net.
    const glm::vec3 pressed = state.cart.pose.position;
    for (int i = 0; i < 50; ++i) {
        update_game(state, forward, 0.02f);
    }
    CHECK(horizontal_distance(state.cart.pose.position, pressed) < 0.15f * state.tuning.cart.speed);
}

TEST_CASE("on foot I stop at a cart, and my cart stops at someone on foot") {
    game_state state = started_game(fixture_hub_course());
    state.area.obstacles = upright_obstacles{};
    state.area.cart_obstacles = upright_obstacles{};
    // Walking at my parked cart's side.
    const glm::vec3 side = yaw_left(yaw_direction(state.cart.pose.yaw));
    state.player.position = anchor_on_terrain(state.area, state.cart.pose.position + side * 4.0f);
    state.player.yaw = yaw_towards(state.player.position, state.cart.pose.position);
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 50; ++i) {
        update_game(state, forward, 0.02f);
    }
    CHECK(glm::dot(state.player.position - state.cart.pose.position, side) >=
          state.tuning.cart.body_radius + state.tuning.player.body_radius - 0.01f);
    CHECK(has_audio(state, audio_event_type::player_bump));

    // Someone standing in front of my cart as I drive.
    game_state driving = started_game(fixture_hub_course());
    driving.area.obstacles = upright_obstacles{};
    driving.area.cart_obstacles = upright_obstacles{};
    sit_in_cart(driving);
    const glm::vec3 ahead = yaw_direction(driving.cart.pose.yaw);
    remote_avatar someone;
    someone.position = driving.cart.pose.position + ahead * 8.0f;
    someone.mode = motion_mode::idle;
    driving.remote_avatars[7] = someone;
    for (int i = 0; i < 60; ++i) {
        update_game(driving, forward, 0.02f);
    }
    CHECK(glm::dot(someone.position - driving.cart.pose.position, ahead) >=
          driving.tuning.cart.body_ahead + driving.tuning.cart.body_radius + driving.tuning.player.body_radius - 0.01f);
}
