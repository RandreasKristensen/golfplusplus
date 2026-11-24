#include "doctest.h"

#include "core/effect_cursor.h"
#include "core/input.h"
#include "test_support.h"

TEST_CASE("the right button frees the cursor only while walking on foot in a round") {
    game_state state = started_hole();
    REQUIRE(state.mode == game_mode::walking);
    REQUIRE(on_foot(state));
    CHECK(wants_effect_cursor(state, true));
    CHECK(!wants_effect_cursor(state, false));

    // Not at address, not following a shot.
    for (const game_mode mode : {game_mode::addressing, game_mode::following_shot}) {
        game_state other = state;
        other.mode = mode;
        CHECK(!wants_effect_cursor(other, true));
    }

    // Not sat in a cart, driving or riding.
    game_state seated = state;
    seated.ride.emplace();
    CHECK(!wants_effect_cursor(seated, true));

    // Not in the hub's screens or on its bus.
    game_state in_book = state;
    in_book.hub_flow.screen = hub_screen::book;
    CHECK(!wants_effect_cursor(in_book, true));
    game_state travelling = state;
    travelling.hub_flow.travel_course = "somewhere";
    CHECK(!wants_effect_cursor(travelling, true));
}

TEST_CASE("the look stays still while the cursor is free and for two frames after it is captured again") {
    effect_cursor_state cursor;
    CHECK(!look_blocked(cursor));

    cursor = next_effect_cursor(cursor, true);
    CHECK(cursor.freed);
    CHECK(look_blocked(cursor));
    cursor = next_effect_cursor(cursor, true);
    CHECK(look_blocked(cursor));

    // Let go: the counts polled just before the capture returned are the free cursor's.
    cursor = next_effect_cursor(cursor, false);
    CHECK(!cursor.freed);
    CHECK(look_blocked(cursor));
    cursor = next_effect_cursor(cursor, false);
    CHECK(look_blocked(cursor));
    cursor = next_effect_cursor(cursor, false);
    CHECK(!look_blocked(cursor));
    cursor = next_effect_cursor(cursor, false);
    CHECK(!look_blocked(cursor));

    // Pressed again while settling frees it at once.
    cursor = next_effect_cursor(next_effect_cursor(next_effect_cursor(cursor, true), false), true);
    CHECK(cursor.freed);
    CHECK(cursor.settle_frames == 0);
}

TEST_CASE("a mouse position becomes overlay clip space, y up") {
    input_state input;
    input.mouse_x = 0;
    input.mouse_y = 0;
    CHECK(mouse_clip_position(input, 1280, 720) == glm::vec2(-1.0f, 1.0f));
    input.mouse_x = 1280;
    input.mouse_y = 720;
    CHECK(mouse_clip_position(input, 1280, 720) == glm::vec2(1.0f, -1.0f));
    input.mouse_x = 320;
    input.mouse_y = 180;
    CHECK(mouse_clip_position(input, 1280, 720) == glm::vec2(-0.5f, 0.5f));
}
