#include "doctest.h"

#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/interactables.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <string>

namespace {
game_input pressing_b() {
    game_input input;
    input.move_bag = true;
    return input;
}

game_input picker_steps(const int steps) {
    game_input input;
    input.picker_steps = steps;
    return input;
}

// Stood on foot `distance` behind where my bag is on my cart, clear of its seats.
void stand_by_bag(game_state& state, const float distance) {
    const glm::vec3 bag = bag_position(state);
    state.player.position = bag - yaw_direction(state.cart.pose.yaw) * distance;
    state.player.position.y = terrain_height(state.area, state.player.position);
}

const std::string& club_label(const game_state& state) {
    return state.clubs[state.selected_club].label;
}
}

TEST_CASE("a round starts with my bag on my cart behind the driver's seat") {
    const game_state state = started_hole();
    CHECK(state.bag.where == bag_where::on_cart);
    CHECK(state.bag.seat == cart_seat::driver);
    CHECK(horizontal_distance(bag_position(state), bag_spot(state.cart.pose, cart_seat::driver, state.tuning.cart)) < 0.001f);
}

TEST_CASE("B unstraps my bag onto my back, sets it down at my feet, shoulders it and straps it back on my cart") {
    game_state state = started_hole();
    stand_by_bag(state, 0.5f);
    update_game(state, pressing_b(), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_back);

    // Away from the cart, B sets it down beside me.
    const glm::vec3 cart_spot = bag_spot(state.cart.pose, cart_seat::driver, state.tuning.cart);
    state.player.position = cart_spot + glm::vec3(0.0f, 0.0f, 10.0f);
    update_game(state, pressing_b(), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_ground);
    CHECK(near(horizontal_distance(state.bag.position, state.player.position), state.tuning.player.bag_set_down_distance,
               0.01f));

    // Shouldered from the ground only within reach.
    const glm::vec3 set_down = state.bag.position;
    state.player.position = set_down + glm::vec3(state.tuning.player.bag_reach + 1.0f, 0.0f, 0.0f);
    update_game(state, pressing_b(), 0.016f);
    CHECK(state.bag.where == bag_where::on_ground);
    state.player.position = set_down + glm::vec3(0.5f, 0.0f, 0.0f);
    update_game(state, pressing_b(), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_back);

    // Back at the cart, B straps it on behind the nearer seat.
    state.player.position = bag_spot(state.cart.pose, cart_seat::passenger, state.tuning.cart);
    update_game(state, pressing_b(), 0.016f);
    CHECK(state.bag.where == bag_where::on_cart);
    CHECK(state.bag.seat == cart_seat::passenger);
}

TEST_CASE("with my bag on my back F at my ball is refused with a hint; set down, it addresses") {
    game_state state = started_hole();
    state.bag.where = bag_where::on_back;
    enter_addressing(state);
    CHECK(state.mode == game_mode::walking);
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_hud_bag_on_back);
    CHECK(lookup_text(shipped_text_assets(), text_hud_bag_on_back) == "SET YOUR BAG DOWN (B)");

    update_game(state, pressing_b(), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_ground);
    // F at my bag opens its picker; a step nearer my ball than my bag, F addresses it.
    update_game(state, interact_input(), 0.016f);
    CHECK(state.picker.has_value());
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.picker);
    state.player.position += safe_normalize(horizontal(state.ball->position - state.player.position), glm::vec3(1.0f, 0.0f, 0.0f)) * 1.2f;
    enter_addressing(state);
    CHECK(state.mode == game_mode::addressing);
}

TEST_CASE("clubs change only at my bag, within reach, in its picker: up the list is a longer club") {
    game_state state = started_hole();
    while (club_label(state) != "7I") {
        state.selected_club += 1;
    }
    // Out of reach F at the bag does nothing, and no wheel changes clubs.
    stand_by_bag(state, state.tuning.player.bag_reach + 1.0f);
    CHECK(!bag_in_reach(state));
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.picker);
    update_game(state, picker_steps(1), 0.016f);
    CHECK(club_label(state) == "7I");

    stand_by_bag(state, 0.5f);
    const std::optional<interactable> near_bag = nearest_interactable(state);
    REQUIRE(near_bag.has_value());
    CHECK(near_bag->kind == interactable_kind::bag);
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.picker.has_value());
    CHECK(state.picker->highlighted == state.selected_club);

    // Up is longer, down shorter; the list stops at its ends.
    update_game(state, picker_steps(1), 0.016f);
    CHECK(state.clubs[state.picker->highlighted].label == "6I");
    update_game(state, picker_steps(-2), 0.016f);
    CHECK(state.clubs[state.picker->highlighted].label == "8I");
    update_game(state, picker_steps(40), 0.016f);
    CHECK(state.picker->highlighted == 0U);
    // The picker takes W and S: I do not walk while it is open.
    const glm::vec3 standing = state.player.position;
    game_input walk;
    walk.forward_held = true;
    update_game(state, walk, 0.1f);
    CHECK(horizontal_distance(state.player.position, standing) < 0.001f);

    update_game(state, picker_steps(-3), 0.016f);
    update_game(state, interact_input(), 0.016f);
    CHECK(!state.picker);
    CHECK(club_label(state) == "4H");

    // Escape closes it with the club in hand kept.
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.picker.has_value());
    update_game(state, picker_steps(2), 0.016f);
    game_input escape;
    escape.cancel = true;
    update_game(state, escape, 0.016f);
    CHECK(!state.picker);
    CHECK(club_label(state) == "4H");
}

TEST_CASE("my bag stays where I set it down through holing out and the next hole") {
    game_state state = started_hole();
    stand_by_bag(state, 0.5f);
    update_game(state, pressing_b(), 0.016f);
    state.player.position = state.ball->position + glm::vec3(1.0f, 0.0f, 0.0f);
    update_game(state, pressing_b(), 0.016f);
    REQUIRE(state.bag.where == bag_where::on_ground);
    const glm::vec3 set_down = state.bag.position;

    complete_current_hole(state);
    CHECK(state.bag.where == bag_where::on_ground);
    CHECK(state.bag.position == set_down);
    state.ball.reset();
    REQUIRE(tee_up(state, 1, false));
    CHECK(state.bag.where == bag_where::on_ground);
    CHECK(state.bag.position == set_down);
}
