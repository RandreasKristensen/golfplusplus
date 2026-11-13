#include "doctest.h"

#include "core/render_frame.h"
#include "core/startup_flow.h"
#include "game/bag_handling.h"
#include "game/gear_paint.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/interactables.h"
#include "game/ride_sync.h"
#include "game/text_ids.h"
#include "physics/cart_rollover.h"
#include "renderer/cart_batch.h"

#include "test_support.h"

#include <algorithm>

#include <glm/vec4.hpp>

namespace {
constexpr std::uint64_t me = 1;
constexpr std::uint64_t other = 2;

// Online on the fixture hub course with my rows, and another player's cart
// parked `offset` from me.
game_state online_with_other_cart(const glm::vec3& offset) {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = me;
    state.online.server_now = 1000000000;
    room_player self;
    self.name = "ME";
    self.round_strokes = std::vector<int>(state.course_holes.size(), 0);
    self.motion = current_motion(state);
    self.motion_at = state.online.server_now;
    state.online.players[me] = self;
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    state.online.carts[other] = room_cart{state.player.position + offset, 0.0f, 0};
    return state;
}

const net_command* find_command(const game_state& state, const net_command_type type) {
    for (const net_command& command : state.net_commands) {
        if (command.type == type) {
            return &command;
        }
    }
    return nullptr;
}

render_data frame_for(const game_state& state) {
    return make_render_data(state, shipped_text_assets(), shipped_content().skills, render_meshes{}, test_props(), 60.0f, false);
}

game_input pressing(bool game_input::*key) {
    game_input input;
    input.*key = true;
    return input;
}
}

TEST_CASE("a cart going over rolls past its tipping point, lands on its side, bounces a little and lies still") {
    const cart_spec& spec = *shipped_content().items.find("golf_cart")->cart;
    cart_fall fall = start_cart_fall(spec);
    CHECK(fall.angle == 0.0f);
    CHECK(fall.rate > 0.0f);
    float most = 0.0f;
    bool bounced = false;
    float seconds = 0.0f;
    for (; seconds < 5.0f && !fall.resting; seconds += cart_fall_step_seconds) {
        fall = step_cart_fall(fall, spec, 0.2f, cart_fall_step_seconds);
        most = std::max(most, fall.angle);
        bounced = bounced || fall.rate < 0.0f;
        CHECK(fall.angle <= cart_lying_angle);
    }
    CHECK(fall.resting);
    CHECK(bounced);
    CHECK(near(fall.angle, cart_lying_angle, 0.0001f));
    CHECK(near(most, cart_lying_angle, 0.0001f));
    // Over in about a second, as a real cart topples.
    CHECK(seconds > 0.3f);
    CHECK(seconds < 2.5f);
}

TEST_CASE("a cart going over is drawn lying on the ground, never through it nor in the air, on flat ground and on a slope") {
    const prop_model& model = test_props().at("golf_cart");
    const auto lowest_over = [&](const render_cart& cart, const auto& ground) {
        prop_batch batch;
        append_cart_model(batch, cart, model, test_scene_colors());
        float lowest = 1000.0f;
        for (const prop_vertex& vertex : batch.opaque()) {
            lowest = std::min(lowest, vertex.position.y - ground(vertex.position));
        }
        return lowest;
    };
    const auto flat = [](const glm::vec3&) { return 2.0f; };
    const auto slope = [](const glm::vec3& point) { return 2.0f + 0.35f * point.x; };
    for (const bool left : {true, false}) {
        for (const float angle : {0.4f, 1.0f, cart_lying_angle}) {
            for (const int ground : {0, 1}) {
                const render_cart cart = ground == 0 ? cart_rolled(glm::vec3(0.0f), 0.3f, left, angle, model, flat)
                                                     : cart_rolled(glm::vec3(0.0f), 0.3f, left, angle, model, slope);
                const float lowest = ground == 0 ? lowest_over(cart, flat) : lowest_over(cart, slope);
                // Its lowest point just on the ground: a hair over it at most.
                CHECK(lowest > -0.06f);
                CHECK(lowest < 0.12f);
            }
        }
        // Lying, its roof points to the side it went over to.
        const render_cart lying = cart_rolled(glm::vec3(0.0f), 0.0f, left, cart_lying_angle, model, flat);
        const glm::vec3 side = yaw_left(yaw_direction(0.0f)) * (left ? 1.0f : -1.0f);
        CHECK(glm::dot(lying.up, side) > 0.95f);
    }
}

TEST_CASE("my cart going over is seen falling onto its side from upright") {
    game_state state = started_game(fixture_hub_course());
    update_game(state, game_input{}, 0.02f);
    state.cart.motion.condition = cart_condition::on_left_side;
    update_game(state, game_input{}, 0.02f);
    const float early = carts_in_world(state).front().fall_angle;
    CHECK(early < 0.5f);
    for (int i = 0; i < 150; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(near(carts_in_world(state).front().fall_angle, cart_lying_angle, 0.0001f));
    CHECK(state.cart_falls.at(0).fall.resting);
}

TEST_CASE("bags and carts wear their owner's favourite colour, else one picked by account") {
    CHECK(gear_paint_index(gear_paint{3, 7}, 8) == 2U);
    CHECK(gear_paint_index(gear_paint{0, 7}, 8) == 7U);
    CHECK(gear_paint_index(gear_paint{0, 9}, 8) == 1U);
    CHECK(gear_paint_index(gear_paint{40, 9}, 8) == 7U);
    const figure_colors& figures = test_scene_colors().figures;
    REQUIRE(figures.bag_paints.size() == figures.cart_paints.size());
    // The bag a darker shade than its cart.
    for (std::size_t i = 0; i < figures.bag_paints.size(); ++i) {
        CHECK(luminance(figures.bag_paints[i]) < luminance(figures.cart_paints[i]));
    }

    game_state state = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    state.favourite_colour = 4;
    state.online.players[other].favourite_colour = 2;
    const gear_paint mine = gear_paint_of(state, 0);
    CHECK(mine.favourite == 4U);
    CHECK(gear_paint_of(state, other).favourite == 2U);
    const render_data data = frame_for(state);
    REQUIRE(data.carts.size() == 2U);
    std::vector<std::uint8_t> favourites;
    for (const render_cart& cart : data.carts) {
        favourites.push_back(cart.paint.favourite);
    }
    CHECK(std::count(favourites.begin(), favourites.end(), std::uint8_t{4}) == 1);
    CHECK(std::count(favourites.begin(), favourites.end(), std::uint8_t{2}) == 1);
}

TEST_CASE("carrying my bag I have no cart; B straps it behind another's free seat, and their cart takes it along") {
    game_state state = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    state.takes_cart = false;
    state.online.carts.erase(me);
    strap_bag_on_cart(state);
    CHECK(!own_cart(state));
    REQUIRE(bag_on_back(state));

    const cart_pose theirs = *shown_cart(state, other);
    state.player.position = anchor_on_terrain(state.area, bag_spot(theirs, cart_seat::passenger, state.tuning.cart));
    state.net_commands.clear();
    update_game(state, pressing(&game_input::move_bag), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_cart);
    CHECK(state.bag.cart_owner == other);
    CHECK(state.bag.seat == cart_seat::passenger);
    const net_command* placed = find_command(state, net_command_type::place_bag);
    REQUIRE(placed != nullptr);
    CHECK(placed->bag.cart_owner == other);
    // It is being strapped on while I keep walking.
    REQUIRE(state.handling.has_value());
    const glm::vec3 standing = state.player.position;
    update_game(state, pressing(&game_input::forward_held), 0.1f);
    CHECK(horizontal_distance(state.player.position, standing) > 0.001f);
    finish_handling(state);

    // Their cart drives off with my bag on it.
    state.online.carts[other].position += glm::vec3(0.0f, 0.0f, 20.0f);
    CHECK(horizontal_distance(bag_position(state), bag_spot(*shown_cart(state, other), cart_seat::passenger, state.tuning.cart)) <
          0.001f);
}

TEST_CASE("B at a spot where another's bag is strapped says there is no room") {
    game_state state = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    state.bag = bag_place{};
    // Their own bag behind their driver's seat.
    state.online.bags[other] = room_bag{bag_place{bag_where::on_cart, cart_seat::driver, 0, glm::vec3(0.0f), 0.0f}, "driver"};
    state.player.position =
        anchor_on_terrain(state.area, bag_spot(*shown_cart(state, other), cart_seat::driver, state.tuning.cart));
    update_game(state, pressing(&game_input::move_bag), 0.016f);
    CHECK(bag_on_back(state));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_hud_bag_spot_taken);
}

TEST_CASE("my cart crashed onto its side, F at it takes my bag off onto my back and I walk on") {
    game_state state = started_game(fixture_hub_course());
    state.cart.motion.condition = cart_condition::on_left_side;
    update_game(state, game_input{}, 0.02f);
    REQUIRE(state.bag.where == bag_where::on_cart);
    CHECK(!bag_in_reach(state).has_value());
    state.player.position = anchor_on_terrain(state.area, bag_spot(state.cart.pose, cart_seat::driver, state.tuning.cart) -
                                                              yaw_direction(state.cart.pose.yaw) * 0.6f);
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    CHECK(near_me->kind == interactable_kind::fallen_bag);
    update_game(state, interact_input(), 0.02f);
    CHECK(bag_on_back(state));
    finish_handling(state);
    // Away from the cart, B sets it down to play on.
    state.player.position += glm::vec3(20.0f, 0.0f, 0.0f);
    update_game(state, pressing(&game_input::move_bag), 0.02f);
    CHECK(state.bag.where == bag_where::on_ground);
}

TEST_CASE("offline a sunk cart is towed back without my bag: it is left where the cart went in") {
    game_state state = started_game(fixture_hub_course());
    const glm::vec3 sank_at = state.cart.pose.position + glm::vec3(0.0f, 0.0f, 40.0f);
    state.cart.pose.position = anchor_on_terrain(state.area, sank_at);
    state.cart.motion.condition = cart_condition::sunk;
    REQUIRE(state.bag.where == bag_where::on_cart);
    for (int i = 0; i < static_cast<int>(my_cart_spec(state).tow_seconds / 0.05f) + 10; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    REQUIRE(state.cart.motion.condition == cart_condition::upright);
    CHECK(state.bag.where == bag_where::on_ground);
    CHECK(horizontal_distance(state.bag.position, sank_at) < 2.0f);
}

TEST_CASE("swapping clubs at my bag takes a moment: the club goes in and another comes out, and others see it too") {
    game_state state = started_hole();
    state.player.position = bag_position(state) - yaw_direction(state.cart.pose.yaw) * 0.5f;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.picker.has_value());
    game_input shorter;
    shorter.picker_steps = -1;
    update_game(state, shorter, 0.016f);
    const std::string before = state.clubs[state.selected_club].id;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.handling.has_value());
    CHECK(state.handling->kind == handling_kind::club);
    CHECK(state.handling->club_out == before);
    CHECK(state.handling->club_in == state.clubs[state.selected_club].id);
    // Halfway it is drawn on its way, and F does nothing until it is done.
    update_game(state, game_input{}, 0.5f * state.tuning.player.club_swap_seconds);
    const render_data data = frame_for(state);
    CHECK(!data.handled_props.empty());
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.picker.has_value());
    finish_handling(state);
    update_game(state, interact_input(), 0.016f);
    CHECK(state.picker.has_value());

    // Another's bag row moving, or their club changing, is shown handled.
    game_state online = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    room_player them;
    them.name = "THEM";
    net_motion motion;
    motion.position = online.player.position + glm::vec3(5.0f, 0.0f, 0.0f);
    them.motion = motion;
    them.motion_at = online.online.server_now;
    online.online.players[other] = them;
    online.online.bags[other] = room_bag{bag_place{}, "driver"};
    update_game(online, game_input{}, 0.02f);
    REQUIRE(online.remote_avatars.count(other) == 1U);
    CHECK(!online.remote_avatars.at(other).handling.has_value());
    online.online.bags[other] = room_bag{bag_place{bag_where::on_ground, cart_seat::driver, 0, motion.position, 0.0f}, "driver"};
    update_game(online, game_input{}, 0.02f);
    REQUIRE(online.remote_avatars.at(other).handling.has_value());
    CHECK(online.remote_avatars.at(other).handling->kind == handling_kind::bag);
    for (int i = 0; i < 60; ++i) {
        update_game(online, game_input{}, 0.05f);
    }
    online.online.bags[other].club_in_hand = "putter";
    update_game(online, game_input{}, 0.02f);
    REQUIRE(online.remote_avatars.at(other).handling.has_value());
    CHECK(online.remote_avatars.at(other).handling->kind == handling_kind::club);
    CHECK(online.remote_avatars.at(other).handling->club_in == "putter");
}

TEST_CASE("riding together: the driver sends their cart to the rider alone, far more often, and the rider rides on it") {
    // Driving my cart with someone in my passenger seat.
    game_state driver = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    sit_in_cart(driver);
    driver.online.carts[me] = room_cart{driver.cart.pose.position, driver.cart.pose.yaw, other, cart_condition::upright, me};
    driver.net_commands.clear();
    game_input forward;
    forward.forward_held = true;
    for (int i = 0; i < 50; ++i) {
        update_game(driver, forward, 0.02f);
    }
    const auto count = [&](const net_command_type type) {
        return std::count_if(driver.net_commands.begin(), driver.net_commands.end(),
                             [type](const net_command& command) { return command.type == type; });
    };
    const float interval = driver.tuning.net.ride_motion_interval_seconds;
    CHECK(count(net_command_type::ride_motion) >= static_cast<long>(0.9f / interval));
    CHECK(count(net_command_type::ride_motion) > 3 * count(net_command_type::motion));
    // Nobody aboard: none.
    driver.online.carts[me].passenger = 0;
    driver.net_commands.clear();
    for (int i = 0; i < 20; ++i) {
        update_game(driver, forward, 0.02f);
    }
    CHECK(count(net_command_type::ride_motion) == 0);

    // Riding in another's cart: it is where its latest ride motion carries it.
    game_state rider = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    stand_at_door(rider, *shown_cart(rider, other), cart_seat::passenger);
    walk_in(rider);
    REQUIRE(rider.ride.has_value());
    rider.online.carts[other].passenger = me;
    for (int i = 0; i < 50; ++i) {
        update_game(rider, game_input{}, 0.02f);
    }
    REQUIRE(rider.ride.has_value());
    const cart_tuning& tuning = rider.tuning.cart;
    net_motion at_wheel;
    at_wheel.mode = motion_mode::cart;
    at_wheel.position = seat_position(*shown_cart(rider, other), cart_seat::driver, tuning) + glm::vec3(0.0f, 0.0f, 4.0f);
    at_wheel.yaw = 0.0f;
    at_wheel.speed = 5.0f;
    rider.online.carts[other].driver = other;
    rider.online.ride = room_ride{other, at_wheel, rider.online.server_now};
    update_game(rider, game_input{}, 0.02f);
    REQUIRE(rider.ride_shown.has_value());
    for (int i = 0; i < 20; ++i) {
        update_game(rider, game_input{}, 0.02f);
    }
    const cart_pose shown = *shown_cart(rider, other);
    CHECK(horizontal_distance(seat_position(shown, cart_seat::driver, tuning), at_wheel.position) < 0.05f);
    CHECK(near(horizontal(rider.player.position), horizontal(seat_position(shown, cart_seat::passenger, tuning)), 0.001f));
    // Carried on along it as time goes by.
    rider.online.server_now += 200000;
    update_game(rider, game_input{}, 0.02f);
    CHECK(horizontal_distance(seat_position(*shown_cart(rider, other), cart_seat::driver, tuning), at_wheel.position) > 0.5f);
}

TEST_CASE("offline carrying starts without a cart") {
    game_state state = started_game(fixture_hub_course());
    state.takes_cart = false;
    REQUIRE(start_course(state, fixture_hub_course()));
    CHECK(!own_cart(state)); CHECK(bag_on_back(state)); CHECK(carts_in_world(state).empty());
}

TEST_CASE("at a seat's door the strip at the bottom says walking in sits me down") {
    game_state state = started_game(fixture_hub_course());
    CHECK(frame_for(state).key_hints == nullptr);
    stand_at_door(state, state.cart.pose, cart_seat::passenger);
    CHECK(std::string(frame_for(state).key_hints) == text_hud_seat_door_drive);

    game_state online = online_with_other_cart(glm::vec3(30.0f, 0.0f, 0.0f));
    stand_at_door(online, *shown_cart(online, other), cart_seat::driver);
    CHECK(std::string(frame_for(online).key_hints) == text_hud_seat_door_ride);
}
