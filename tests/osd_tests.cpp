#include "doctest.h"

#include "core/render_frame.h"
#include "game/address_play.h"
#include "game/text_assets.h"
#include "physics/vector_math.h"
#include "renderer/hud_overlay.h"
#include "renderer/osd_overlay.h"

#include "test_support.h"

#include <cmath>
#include <set>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
render_data frame_for(const game_state& state) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), 60.0f, true);
}

glm::mat4 view_proj_of(const render_data& data, const float aspect) {
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    return glm::perspective(glm::radians(data.camera_fov_degrees), aspect, 0.05f, 2000.0f) * view;
}

// At address in the target view with the 7-iron, the face opened `face_degrees`.
game_state addressing(const handedness hand, const float face_degrees) {
    game_state state = started_hole();
    state.hand = hand;
    while (state.clubs[state.selected_club].label != "7I") {
        state.selected_club += 1;
    }
    enter_addressing(state);
    CHECK(state.mode == game_mode::addressing);
    state.address.setup.face_open = glm::radians(face_degrees);
    refresh_shot_preview(state);
    return state;
}

constexpr float aspects[] = {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f};
}

TEST_CASE("in the target view the start line is in view from the ball, fading out where it ends") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        const game_state state = addressing(hand, 0.0f);
        const render_data data = frame_for(state);
        REQUIRE(data.osd_line.size() > 2U);
        for (const float aspect : aspects) {
            const glm::mat4 view_proj = view_proj_of(data, aspect);
            // The ball is just under the picture; where the line ends is in it.
            const std::optional<glm::vec2> end = project_to_screen(view_proj, data.osd_line.back());
            REQUIRE(end.has_value());
            CHECK(std::abs(end->x) < 0.9f);
            CHECK(std::abs(end->y) < 0.9f);
            overlay_batch batch;
            batch.grid = overlay_grid{320, 240};
            draw_shot_preview(batch, data, view_proj);
            REQUIRE(batch.vertices.size() >= 2U * overlay_vertices_per_quad);
            // Plain where it comes into the picture, nearly gone at its end.
            const float first = batch.vertices.front().color.a;
            const float last = batch.vertices.back().color.a;
            CHECK(first > 0.5f);
            CHECK(last < 0.2f);
        }
    }
}

TEST_CASE("a fade starts the line away from the golfer's lead side, a draw towards it, for either hand") {
    for (const handedness hand : {handedness::right, handedness::left}) {
        const game_state stock_state = addressing(hand, 0.0f);
        const render_data stock = frame_for(stock_state);
        const render_data fade = frame_for(addressing(hand, 4.0f));
        const render_data draw = frame_for(addressing(hand, -4.0f));
        const glm::mat4 view_proj = view_proj_of(stock, 16.0f / 9.0f);
        const float stock_x = project_to_screen(view_proj, stock.osd_line.back())->x;
        const float fade_x = project_to_screen(view_proj, fade.osd_line.back())->x;
        const float draw_x = project_to_screen(view_proj, draw.osd_line.back())->x;
        // On screen right is the right-hander's fade side, the left-hander's draw side.
        const float fade_side = hand == handedness::right ? 1.0f : -1.0f;
        CHECK((fade_x - stock_x) * fade_side > 0.01f);
        CHECK((draw_x - stock_x) * fade_side < -0.01f);
    }
}

TEST_CASE("the OSD line is whole target pixels, white over a one-pixel black edge") {
    overlay_batch batch;
    batch.grid = overlay_grid{320, 240};
    draw_osd_line(batch, glm::vec2(-0.5f, -0.5f), glm::vec2(0.3f, 0.2f));
    REQUIRE(!batch.vertices.empty());
    const glm::vec2 pixel(2.0f / 320.0f, 2.0f / 240.0f);
    std::set<std::pair<int, int>> white;
    std::size_t black_quads = 0;
    for (std::size_t i = 0; i < batch.vertices.size(); i += overlay_vertices_per_quad) {
        glm::vec2 low(1.0e9f);
        glm::vec2 high(-1.0e9f);
        for (std::size_t k = i; k < i + overlay_vertices_per_quad; ++k) {
            low = glm::min(low, batch.vertices[k].position);
            high = glm::max(high, batch.vertices[k].position);
        }
        const glm::vec3 color(batch.vertices[i].color);
        if (color.r > 0.9f) {
            // One pixel, on the target's grid, after every edge quad.
            CHECK(near(high.x - low.x, pixel.x, 1.0e-5f));
            CHECK(near(high.y - low.y, pixel.y, 1.0e-5f));
            const float column = (low.x + 1.0f) / pixel.x;
            CHECK(near(column, std::round(column), 1.0e-3f));
            white.insert({static_cast<int>(std::round(column)), static_cast<int>(std::round((low.y + 1.0f) / pixel.y))});
            CHECK(black_quads > 0U);
        } else {
            CHECK(color.r < 0.05f);
            CHECK(near(high.x - low.x, 3.0f * pixel.x, 1.0e-5f));
            ++black_quads;
        }
    }
    // A connected line from end to end: as many pixels as its longer side.
    CHECK(white.size() == static_cast<std::size_t>(std::round(0.8f / pixel.x)) + 1U);
}

TEST_CASE("the swing clock turns to its warning tone past 12 and with a mistimed hand, and a putter's has no overswing band") {
    const text_assets& text = shipped_text_assets();
    const auto warning_quads = [&](const float backswing, const float backswing_max, const bool mistimed = false) {
        overlay_batch batch;
        batch.grid = overlay_grid{320, 240};
        draw_swing_clock(batch, text, backswing, backswing_max, std::nullopt, mistimed);
        std::size_t count = 0;
        for (std::size_t i = 0; i < batch.vertices.size(); i += overlay_vertices_per_quad) {
            const glm::vec4 color = batch.vertices[i].color;
            count += color.r > 0.85f && color.g < 0.3f && color.b < 0.2f ? 1 : 0;
        }
        return count;
    };
    CHECK(warning_quads(1.0f, 1.0f) == 0U);
    const std::size_t band = warning_quads(1.0f, 1.1f);
    CHECK(band > 0U);
    CHECK(warning_quads(1.1f, 1.1f) > band);
    CHECK(warning_quads(0.8f, 1.0f, true) > 0U);
}

TEST_CASE("the OSD shows where my bag is, and the setup word over the ball") {
    game_state state = started_hole();
    render_data data = frame_for(state);
    REQUIRE(data.bag_shown.has_value());
    CHECK(*data.bag_shown == bag_pictogram::cart);
    enter_addressing(state);
    data = frame_for(state);
    CHECK(data.setup_word == "STOCK");
    state.address.setup.face_open = glm::radians(state.tuning.golfer.curve_full_degrees);
    state.address.setup.ball_position -= state.tuning.golfer.ball_full;
    CHECK(frame_for(state).setup_word == "PUNCH FADE");
}
