#include "doctest.h"

#include "audio/audio_manifest.h"
#include "game/cart_seats.h"
#include "game/course_session.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/interactables.h"
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
    const course_world_hole_start& start = state.world.hole_starts.front();
    const cart_pose parked = parked_cart_pose(start.position, state.holes.front().pin_position, 0, state.tuning.cart);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(parked.position), 0.0001f));
    CHECK(near(state.cart.pose.position.y, terrain_height(state.area, state.cart.pose.position), 0.0001f));
    CHECK(near(state.cart.pose.yaw, yaw_towards(start.position, state.holes.front().pin_position), 0.0001f));
    CHECK(on_foot(state));
    CHECK(my_cart_speed(state) == 0.0f);
    // Beside the hole, not on its start: the first press there tees up on hole 1.
    const float apart = horizontal_distance(state.cart.pose.position, start.position);
    CHECK(apart > start.interaction_radius);
    CHECK(apart < 15.0f);
    CHECK(!nearest_seat(state).has_value());
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::tee);
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

TEST_CASE("a tee nearer than the seat tees up there; a seat nearer than it is stepped into") {
    game_state state = started_game(fixture_hub_course());
    const cart_tuning& tuning = state.tuning.cart;
    const course_world_hole_start& start = state.world.hole_starts.front();
    // The cart parked with its wheel just off the start, within both reaches.
    const glm::vec3 across = glm::vec3(1.0f, 0.0f, 0.0f);
    state.cart.pose = cart_around_seat(start.position + across * 2.0f, 0.0f, cart_seat::driver, tuning);

    game_state at_start = state;
    at_start.player.position = anchor_on_terrain(at_start.area, start.position + across * 0.5f);
    REQUIRE(nearest_seat(at_start).has_value());
    update_game(at_start, interact_input(), 0.02f);
    CHECK(ball_in_play(at_start));
    CHECK(on_foot(at_start));

    game_state at_seat = state;
    at_seat.player.position = anchor_on_terrain(at_seat.area, start.position + across * 1.7f);
    const std::vector<interactable> in_reach = interactables_in_reach(at_seat);
    REQUIRE(std::any_of(in_reach.begin(), in_reach.end(),
                        [](const interactable& thing) { return thing.kind == interactable_kind::tee; }));
    update_game(at_seat, interact_input(), 0.02f);
    CHECK(!at_seat.ball.has_value());
    CHECK(at_seat.ride.has_value());
}

TEST_CASE("the cart stays where I left it when I tee up and when I give the hole up") {
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

    REQUIRE(tee_up(state, 0, false));
    CHECK(near(state.cart.pose.position, left_at.position));
    give_up_hole(state);
    CHECK(near(state.cart.pose.position, left_at.position));
    CHECK(on_foot(state));
}

TEST_CASE("backing up is slower than driving forward, and the cart only turns while it moves") {
    game_state state = started_hole();
    // Sat on the tee, backing into open ground.
    set_solids(state.area, {});
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
    // Never past its reverse share of its top speed.
    CHECK(my_cart_speed(state) >= -my_cart_top_speed(state) * my_cart_spec(state).reverse_share - 0.0001f);
}

TEST_CASE("interact at the cart's wheel only steps out, and the cart never passes its top speed") {
    game_state state = started_hole();
    sit_in_cart(state);
    game_input drive;
    drive.forward_held = true;
    for (int i = 0; i < 40; ++i) {
        drive.interact = i == 20;
        update_game(state, drive, 0.05f);
        CHECK(std::abs(my_cart_speed(state)) <= my_cart_top_speed(state) + 0.0001f);
    }
    CHECK(on_foot(state));
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
    CHECK(my_cart_speed(state) > 0.0f);

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

TEST_CASE("trees, poles, signs, nets, flagsticks and tee boxes stand in the way of players and carts; a tee box's top is walkable") {
    const game_state state = started_game(fixture_hub_course());
    const play_area& area = state.area;
    std::size_t fence_poles = 0;
    std::size_t nets = 0;
    for (const course_world_fence& fence : area.fences) {
        fence_poles += fence.poles.size();
        nets += fence.poles.size() - 1;
    }
    CHECK(solids_of(area, solid_kind::trunk).size() == area.trees.size());
    CHECK(solids_of(area, solid_kind::pole).size() == 2 * area.signs.size() + fence_poles + area.floodlights.size());
    CHECK(solids_of(area, solid_kind::flagstick).empty());  // by the flags (flagstick_solids)
    CHECK(solids_of(area, solid_kind::lamp).size() == area.floodlights.size());
    CHECK(solids_of(area, solid_kind::board).size() == area.signs.size());
    CHECK(solids_of(area, solid_kind::net).size() == nets);
    CHECK(solids_of(area, solid_kind::tee_box).size() == area.tee_boxes.size());
    for (const solid& thing : area.solids) {
        const bool body = thing.kind != solid_kind::leaves && thing.kind != solid_kind::lamp;
        CHECK(thing.stops_walker == body);
        CHECK(thing.stops_cart == body);
        CHECK(thing.walkable == (thing.kind == solid_kind::tee_box));
    }
    // Every tee box stands taller than a cart can climb.
    for (const solid& box : solids_of(area, solid_kind::tee_box)) {
        CHECK(box.stops_ball);
        CHECK(solid_top(box) - terrain_height(area, box.base - box.along * (box.half_length + 0.5f)) >
              state.tuning.cart.step_up);
    }
}

TEST_CASE("walking into a tree stops me at its trunk, with one bump heard") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    const glm::vec3 trunk = state.player.position + ahead * 2.0f;
    set_solids(state.area, {test_post(trunk, 0.4f)});
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

namespace {
// A tee box's worth of box on the ground at `middle`, 6 m along `along` and
// 4 m across, its walkable top `height` up.
solid platform(const play_area& area, const glm::vec3& middle, const glm::vec3& along, const float height) {
    solid box = test_box(anchor_on_terrain(area, middle), along, 3.0f, 2.0f, height);
    box.walkable = true;
    return box;
}
}

TEST_CASE("driving into a tee box crashes the cart: it stops at it, bounces back and is heard") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    sit_in_cart(state);
    const cart_tuning& tuning = state.tuning.cart;
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    const glm::vec3 box = state.cart.pose.position + ahead * 12.0f;
    // Taller than the cart's step-up.
    set_solids(state.area, {platform(state.area, box, ahead, 2.0f * tuning.step_up)});
    game_input forward;
    forward.forward_held = true;
    bool crashed = false;
    for (int i = 0; i < 300 && !crashed; ++i) {
        const float speed = my_cart_speed(state);
        update_game(state, forward, 0.02f);
        crashed = has_audio(state, audio_event_type::cart_crash);
        if (crashed) {
            CHECK(speed >= tuning.crash_sound_speed);
            CHECK(my_cart_speed(state) <= 0.0f);
        }
    }
    CHECK(crashed);
    const float front = glm::dot(state.cart.pose.position - box, ahead) + tuning.body_front;
    CHECK(front <= -3.0f + 0.01f);
    CHECK(!has_audio(state, audio_event_type::player_bump));

    // On foot it is a step: I walk up onto it and across its top.
    game_state walker = started_game(fixture_hub_course());
    set_solids(walker.area, {});
    const glm::vec3 start = walker.player.position;
    const glm::vec3 path = yaw_direction(walker.player.yaw);
    const solid tee = platform(walker.area, start + path * 4.0f, path, 2.0f * tuning.step_up);
    REQUIRE(tee.height <= walker.tuning.player.step_up);
    set_solids(walker.area, {tee});
    game_input walk;
    walk.forward_held = true;
    bool on_top = false;
    for (int i = 0; i < 20; ++i) {
        update_game(walker, walk, 0.02f);
        on_top = on_top || near(walker.player.position.y, solid_top(tee), 0.001f);
    }
    CHECK(horizontal_distance(walker.player.position, start) > 0.9f * walker.tuning.player.walk_speed * 0.4f);
    CHECK(on_top);
}

TEST_CASE("a cart climbs a kerb lower than its step-up and drives on along its top") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    sit_in_cart(state);
    const cart_tuning& tuning = state.tuning.cart;
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    const solid kerb = platform(state.area, state.cart.pose.position + ahead * 8.0f, ahead, 0.8f * tuning.step_up);
    set_solids(state.area, {kerb});
    game_input forward;
    forward.forward_held = true;
    bool on_top = false;
    for (int i = 0; i < 250; ++i) {
        update_game(state, forward, 0.02f);
        on_top = on_top || near(state.cart.pose.position.y, solid_top(kerb), 0.001f);
    }
    CHECK(on_top);
    CHECK(!has_audio(state, audio_event_type::cart_crash));
    CHECK(glm::dot(state.cart.pose.position - kerb.base, ahead) > 3.0f);
}

TEST_CASE("walking off a ledge higher than a step I fall, and land on the ground under it") {
    game_state state = started_game(fixture_hub_course());
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    // I stand on a high platform's top, near its far edge.
    const float height = 3.0f * state.tuning.player.step_up;
    const solid ledge = platform(state.area, state.player.position - ahead * 2.5f, ahead, height);
    set_solids(state.area, {ledge});
    state.player.position.y = solid_top(ledge);
    game_input walk;
    walk.forward_held = true;
    int airborne = 0;
    float lowest_speed = 0.0f;
    for (int i = 0; i < 40; ++i) {
        update_game(state, walk, 0.02f);
        airborne += state.player.vertical.grounded ? 0 : 1;
        lowest_speed = std::min(lowest_speed, state.player.vertical.speed);
    }
    // A fall of this height takes sqrt(2 h / g) seconds.
    const float fall_seconds = std::sqrt(2.0f * height / state.tuning.player.jump_gravity);
    CHECK(near(static_cast<float>(airborne) * 0.02f, fall_seconds, 0.03f));
    CHECK(lowest_speed < -0.8f * state.tuning.player.jump_gravity * fall_seconds);
    CHECK(state.player.vertical.grounded);
    CHECK(near(state.player.position.y, terrain_height(state.area, state.player.position), 0.001f));
}

TEST_CASE("a ledge taller than a step stops me walking, and a hop gets me onto it") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    const glm::vec3 start = state.player.position;
    const glm::vec3 ahead = yaw_direction(state.player.yaw);
    const player_tuning& tuning = state.tuning.player;
    const float apex = tuning.jump_speed * tuning.jump_speed / (2.0f * tuning.jump_gravity);
    const float height = 0.5f * (tuning.step_up + apex);
    const solid ledge = platform(state.area, start + ahead * 4.5f, ahead, height);
    set_solids(state.area, {ledge});
    game_input walk;
    walk.forward_held = true;
    for (int i = 0; i < 40; ++i) {
        update_game(state, walk, 0.02f);
    }
    CHECK(state.player.against_obstacle);
    CHECK(glm::dot(state.player.position - ledge.base, ahead) < -3.0f);
    game_input hop = walk;
    hop.jump = true;
    update_game(state, hop, 0.02f);
    for (int i = 0; i < 40; ++i) {
        update_game(state, walk, 0.02f);
    }
    CHECK(near(state.player.position.y, solid_top(ledge), 0.001f));
    CHECK(glm::dot(state.player.position - ledge.base, ahead) > -3.0f);
}

TEST_CASE("driving along a fence the cart scrapes nearly to a stop instead of gliding along it") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    sit_in_cart(state);
    // A net ahead, slanting across the way at 30 degrees.
    const glm::vec3 ahead = yaw_direction(state.cart.pose.yaw);
    const glm::vec3 slant = yaw_direction(state.cart.pose.yaw + glm::radians(60.0f));
    const glm::vec3 mid = state.cart.pose.position + ahead * 6.0f;
    set_solids(state.area, {test_wall(mid - slant * 40.0f, mid + slant * 40.0f, 0.05f)});
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 150; ++i) {
        update_game(state, forward, 0.02f);
    }
    REQUIRE(state.cart.against_obstacle);
    // Throttle held, it barely creeps along the net.
    const glm::vec3 pressed = state.cart.pose.position;
    for (int i = 0; i < 50; ++i) {
        update_game(state, forward, 0.02f);
    }
    CHECK(horizontal_distance(state.cart.pose.position, pressed) < 0.15f * my_cart_top_speed(state));
}

TEST_CASE("on foot I stop at a cart, and my cart stops at someone on foot") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
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
          state.tuning.cart.body_half_width + state.tuning.player.body_radius - 0.01f);
    CHECK(has_audio(state, audio_event_type::player_bump));

    // Someone standing in front of my cart as I drive.
    game_state driving = started_game(fixture_hub_course());
    set_solids(driving.area, {});
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
          driving.tuning.cart.body_front + driving.tuning.player.body_radius - 0.01f);
}

namespace {
// A course of one hole rising `rise` per metre towards +Z, 80 m wide: a cart
// driving along +X has its ground rising to its right.
game_state on_a_slope(const float rise) {
    game_state state = started_game(fixture_hub_course());
    play_hole(state, straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, rise * 200.0f, 200.0f), 80.0f));
    set_solids(state.area, {});
    state.ball.reset();
    return state;
}

// Sat at my cart's wheel at `feet`, facing `yaw`.
void sit_at(game_state& state, const glm::vec3& feet, const float yaw) {
    state.player.position = anchor_on_terrain(state.area, feet);
    state.player.yaw = yaw;
    sit_in_cart(state);
}

game_input held(const bool forward, const bool right) {
    game_input input;
    input.forward_held = forward;
    input.strafe_right_held = right;
    return input;
}
}

TEST_CASE("a hard turn at speed across a steep slope tips my cart over, and I tumble out beside it") {
    game_state state = on_a_slope(std::tan(glm::radians(22.0f)));
    // Across the slope, rising to my right: turning right swings it out downhill.
    sit_at(state, glm::vec3(-35.0f, 0.0f, 100.0f), glm::radians(90.0f));
    // Already at its top speed, straight on.
    const glm::vec3 heading = yaw_direction(state.cart.pose.yaw);
    state.cart.motion.velocity = glm::vec2(heading.x, heading.z) * my_cart_spec(state).top_speed_mps;
    for (int i = 0; i < 20; ++i) {
        update_game(state, held(true, false), 0.05f);
    }
    REQUIRE(driving_cart(state));
    REQUIRE(state.cart.motion.condition == cart_condition::upright);
    REQUIRE(my_cart_speed(state) > 0.9f * my_cart_top_speed(state));
    state.audio_events.clear();
    for (int i = 0; i < 60 && driving_cart(state); ++i) {
        update_game(state, held(true, true), 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::on_left_side);
    CHECK(on_foot(state));
    CHECK(has_audio(state, audio_event_type::cart_tip));
    CHECK(horizontal_distance(state.player.position, state.cart.pose.position) < 2.0f * state.tuning.cart.board_reach);
    // It lies still, and has no seat to step into.
    const cart_pose lying = state.cart.pose;
    for (int i = 0; i < 20; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(near(state.cart.pose.position, lying.position));
    CHECK(!nearest_seat(state).has_value());
}

TEST_CASE("F beside a tipped cart stands it back up over its right_seconds, unless I walk away") {
    game_state state = started_game(fixture_hub_course());
    state.cart.motion.condition = cart_condition::on_right_side;
    const float right_seconds = my_cart_spec(state).right_seconds;
    stand_beside_my_cart(state);
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::tipped_cart);

    game_state walked_off = state;
    update_game(walked_off, interact_input(), 0.02f);
    REQUIRE(walked_off.righting.has_value());
    walked_off.player.position += yaw_left(yaw_direction(walked_off.cart.pose.yaw)) * (3.0f * walked_off.tuning.cart.board_reach);
    for (int i = 0; i < static_cast<int>(right_seconds / 0.05f) + 5; ++i) {
        update_game(walked_off, game_input{}, 0.05f);
    }
    CHECK(walked_off.cart.motion.condition == cart_condition::on_right_side);
    CHECK(!walked_off.righting.has_value());

    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.righting.has_value());
    const int frames = static_cast<int>(right_seconds / 0.05f);
    for (int i = 0; i < frames - 2; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::on_right_side);
    for (int i = 0; i < 4; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::upright);
    CHECK(has_audio(state, audio_event_type::cart_righted));
    CHECK(nearest_seat(state).has_value());
}

TEST_CASE("driven into a pond deeper than its wade depth my cart sinks, I wade out, and it is towed back to its spot") {
    game_state state = started_game(fixture_hub_course());
    hole_data hole = straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 200.0f), 60.0f);
    hole.material_zones = {circle_zone(material_zone_type::water, glm::vec3(0.0f, 0.0f, 60.0f), 15.0f)};
    play_hole(state, hole);
    set_solids(state.area, {});
    state.ball.reset();
    park_cart(state);
    const cart_pose parked = state.cart.pose;
    sit_at(state, glm::vec3(0.0f, 0.0f, 30.0f), 0.0f);
    state.audio_events.clear();
    for (int i = 0; i < 200 && driving_cart(state); ++i) {
        update_game(state, held(true, false), 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::sunk);
    CHECK(on_foot(state));
    CHECK(has_audio(state, audio_event_type::cart_sink));
    CHECK(cart_ground_at(state.area, {}, state.tuning, 0.0f, state.cart.pose.position).water_depth >
          my_cart_spec(state).wade_depth_m);
    CHECK(!nearest_seat(state).has_value());

    const int tow_frames = static_cast<int>(my_cart_spec(state).tow_seconds / 0.05f);
    for (int i = 0; i < tow_frames - 10; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::sunk);
    for (int i = 0; i < 20; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::upright);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(parked.position), 0.001f));
}

TEST_CASE("the same driving replays the same path whatever the frame rate") {
    const auto drive = [](const float dt) {
        game_state state = on_a_slope(0.08f);
        sit_at(state, glm::vec3(-20.0f, 0.0f, 60.0f), glm::radians(80.0f));
        // A second straight, a second turning right, a second straight.
        const int second = static_cast<int>(std::lround(1.0f / dt));
        for (int i = 0; i < 3 * second; ++i) {
            update_game(state, held(true, i >= second && i < 2 * second), dt);
        }
        return state;
    };
    const game_state slow = drive(1.0f / 30.0f);
    const game_state fast = drive(1.0f / 144.0f);
    REQUIRE(driving_cart(slow));
    // At most a step's travel apart: where a frame ends inside the last step.
    const float step_travel = my_cart_top_speed(slow) * cart_step_seconds;
    CHECK(horizontal_distance(slow.cart.pose.position, fast.cart.pose.position) <= step_travel);
    CHECK(horizontal_distance(slow.cart.pose.position, slow.world.hole_starts.front().position) > 10.0f);
    CHECK(near(slow.cart.pose.yaw, fast.cart.pose.yaw, 0.02f));
}

TEST_CASE("my cart's motor plays higher the faster it goes and the harder it pulls") {
    game_state state = started_game(fixture_hub_course());
    set_solids(state.area, {});
    sit_in_cart(state);
    const float idle = motor_pitch(state);
    CHECK(near(idle, state.tuning.cart.motor_idle_pitch, 0.0001f));
    update_game(state, held(true, false), 0.1f);
    const float pulling_away = motor_pitch(state);
    CHECK(pulling_away > idle + 0.5f * state.tuning.cart.motor_load_pitch);
    for (int i = 0; i < 60; ++i) {
        update_game(state, held(true, false), 0.05f);
    }
    CHECK(motor_pitch(state) > pulling_away);
    CHECK(motor_pitch(state) <= state.tuning.cart.motor_top_speed_pitch + state.tuning.cart.motor_load_pitch + 0.0001f);
}
