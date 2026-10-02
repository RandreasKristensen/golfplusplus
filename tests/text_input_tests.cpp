#include "doctest.h"

#include "game/text_assets.h"
#include "game/text_ids.h"
#include "renderer/overlay_batch.h"
#include "renderer/text_input.h"

#include <optional>
#include <string>

namespace {
text_input_state name_field() {
    text_input_state state;
    state.max_length = 5;
    state.allowed_chars = "ABC ";
    state.active = true;
    return state;
}
}

TEST_CASE("text input appends allowed characters and rejects the rest") {
    const text_input_state state = apply_text_input(name_field(), "AxB!C", false);
    CHECK(state.value == "ABC");
}

TEST_CASE("text input stores lowercase as its allowed uppercase") {
    const text_input_state state = apply_text_input(name_field(), "abc", false);
    CHECK(state.value == "ABC");
}

TEST_CASE("text input takes Danish letters and counts characters, not bytes") {
    text_input_state field = name_field();
    field.allowed_chars = u8"ABÆØÅ";
    text_input_state state = apply_text_input(field, u8"æøåAB!", false);
    CHECK(state.value == std::string(u8"ÆØÅAB"));
    state = apply_text_input(state, "", true);
    CHECK(state.value == std::string(u8"ÆØÅA"));
    state = apply_text_input(apply_text_input(state, "", true), "", true);
    CHECK(state.value == std::string(u8"ÆØ"));
}

TEST_CASE("text input stops at max length") {
    const text_input_state state = apply_text_input(name_field(), "ABCABCABC", false);
    CHECK(state.value == "ABCAB");
    CHECK(apply_text_input(state, "C", false).value == "ABCAB");
}

TEST_CASE("text input backspace removes the last character before typing") {
    text_input_state state = apply_text_input(name_field(), "AB", false);
    state = apply_text_input(state, "", true);
    CHECK(state.value == "A");
    state = apply_text_input(state, "C", true);
    CHECK(state.value == "C");
    state = apply_text_input(apply_text_input(state, "", true), "", true);
    CHECK(state.value.empty());
}

TEST_CASE("inactive text input ignores typing and backspace") {
    text_input_state state = name_field();
    state.value = "AB";
    state.active = false;
    const text_input_state next = apply_text_input(state, "C", true);
    CHECK(next.value == "AB");
}

TEST_CASE("text input returns a new state and leaves the input untouched") {
    const text_input_state before = name_field();
    const text_input_state after = apply_text_input(before, "A", false);
    CHECK(before.value.empty());
    CHECK(after.value == "A");
}

TEST_CASE("text input draws a box, the value and a blinking cursor") {
    const std::optional<text_assets> text = load_text_assets(GOLFPP_ASSETS_DIR);
    CHECK(text.has_value());
    if (!text) {
        return;
    }

    text_input_state state = name_field();
    state.allowed_chars = font_charset(text->font);
    state.value = "AB";
    const text_style& style = find_text_style(*text, style_input);

    overlay_batch cursor_on;
    cursor_on.grid = overlay_grid{640, 360};
    draw_text_input(cursor_on, text->font, style, state, ui_rect{glm::vec2(0.0f), glm::vec2(0.4f, 0.08f)}, 0.1f);
    overlay_batch cursor_off;
    cursor_off.grid = overlay_grid{640, 360};
    draw_text_input(cursor_off, text->font, style, state, ui_rect{glm::vec2(0.0f), glm::vec2(0.4f, 0.08f)}, 0.6f);

    // Background + 4 outline segments + one quad per lit pixel (+ cursor).
    const std::size_t lit = static_cast<std::size_t>(find_glyph(text->font, 'A').lit_pixel_count +
                                                     find_glyph(text->font, 'B').lit_pixel_count);
    CHECK(overlay_batch_quad_count(cursor_off) == 5U + lit);
    CHECK(overlay_batch_quad_count(cursor_on) == 6U + lit);

    state.active = false;
    overlay_batch inactive;
    inactive.grid = overlay_grid{640, 360};
    draw_text_input(inactive, text->font, style, state, ui_rect{glm::vec2(0.0f), glm::vec2(0.4f, 0.08f)}, 0.1f);
    CHECK(overlay_batch_quad_count(inactive) == 5U + lit);
}
