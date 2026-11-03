#include "doctest.h"

#include "core/render_frame.h"
#include "game/shot_number.h"
#include "game/shot_simulation.h"
#include "game/text_assets.h"
#include "physics/ball_physics.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"
#include "renderer/hud_overlay.h"
#include "renderer/shot_number_overlay.h"

#include "test_support.h"

#include <algorithm>
#include <cstdio>
#include <string>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace {
const club_definition& club_labelled(const std::string& label) {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    return *std::find_if(clubs.begin(), clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

shot_number number_of(const shot_setup& setup, const std::uint64_t seed, const golfer_profile& golfer,
                      const ball_lie lie = ball_lie::fairway) {
    const game_content& content = shipped_content();
    return *shot_number_for(shot_record{setup, seed, {}}, golfer, lie, 10.0f, content.tuning, content.clubs);
}

// A golfer whose swing never strays: every shot as set up.
golfer_profile perfect_golfer() {
    golfer_profile golfer;
    golfer.spread_scale = 0.0f;
    return golfer;
}

// The share of `count` seeds on which `golfer`'s stock full swing with `label` crits.
float crit_share(const std::string& label, const golfer_profile& golfer, const int count) {
    const club_definition& club = club_labelled(label);
    int crits = 0;
    for (int i = 0; i < count; ++i) {
        crits += number_of(stock_swing(club, shipped_content().tuning), round_shot_seed(99, i), golfer).crit ? 1 : 0;
    }
    return static_cast<float>(crits) / static_cast<float>(count);
}

render_data frame_for(const game_state& state) {
    static const text_assets text = *load_text_assets(asset_root());
    return make_render_data(state, text, shipped_content().skills, render_meshes{}, test_props(), 60.0f, true);
}

glm::mat4 view_proj_of(const render_data& data, const float aspect) {
    const glm::mat4 view = glm::lookAt(data.camera_position, data.camera_target, world_up);
    return glm::perspective(glm::radians(data.camera_fov_degrees), aspect, 0.05f, 2000.0f) * view;
}
}

TEST_CASE("a shot's number is how far it carries as struck in still air at sea level and 15 C, off a level stance") {
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& driver = club_labelled("DR");
    const shot_setup setup = stock_swing(driver, tuning, 1.0f, ball_lie::tee);
    const air_state air = air_at(tuning.physics.air, 0.0f, 15.0f);
    ball_state ball = launched_ball(glm::vec3(0.0f), tuning.scale.ball_physics_radius_meters,
                                    club_strike(driver, setup, ball_on_lie{ball_lie::tee, {}, 0.0f}, tuning).launch);
    float carry = 0.0f;
    for (int step = 0; step < 120 * 30 && carry == 0.0f; ++step) {
        const ball_state next = step_ball_flight(ball, wind_state{}, air, false, 1.0f / 120.0f, tuning.physics);
        if (next.position.y < 0.0f) {
            const float share = ball.position.y / (ball.position.y - next.position.y);
            carry = horizontal_distance(ball.position + (next.position - ball.position) * share, glm::vec3(0.0f));
        }
        ball = next;
    }
    REQUIRE(carry > 200.0f);
    CHECK(near(number_of(setup, 1, perfect_golfer(), ball_lie::tee).meters, carry, 0.01f));
    // The dice move it: another seed of a real swing carries otherwise.
    CHECK(number_of(setup, 1, golfer_profile{}, ball_lie::tee).meters != number_of(setup, 2, golfer_profile{}, ball_lie::tee).meters);
}

TEST_CASE("a putt's number is how far it rolls on a level green, longer the longer the stroke") {
    const club_definition& putter = club_labelled("P");
    const float short_putt = number_of(stock_swing(putter, shipped_content().tuning, 0.2f, ball_lie::green), 1, perfect_golfer(), ball_lie::green).meters;
    const float long_putt = number_of(stock_swing(putter, shipped_content().tuning, 0.8f, ball_lie::green), 1, perfect_golfer(), ball_lie::green).meters;
    CHECK(short_putt > 0.3f);
    CHECK(long_putt > short_putt * 2.0f);
    CHECK(!number_of(stock_swing(putter, shipped_content().tuning, 1.0f, ball_lie::green), 1, perfect_golfer(), ball_lie::green).crit);
}

TEST_CASE("only a full stock swing crits: a fade, a punch, a short swing, an overswing never do, even struck perfectly") {
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& iron = club_labelled("7I");
    const shot_setup stock = stock_swing(iron, tuning);
    CHECK(number_of(stock, 1, perfect_golfer()).crit);
    shot_setup fade = stock;
    fade.face_open = glm::radians(3.0f);
    shot_setup punch = stock;
    punch.ball_position -= 0.3f;
    for (const shot_setup& dialled : {fade, punch, stock_swing(iron, tuning, 0.75f), stock_swing(iron, tuning, 1.1f)}) {
        CHECK(!number_of(dialled, 1, perfect_golfer()).crit);
    }
}

TEST_CASE("crits are rare: a few stock swings in a hundred at the top level, fewer still for a beginner") {
    golfer_profile beginner;
    for (const std::string& skill : {"club.driver", "club.short_irons", "club.wedges", "shape.fade", "shape.draw"}) {
        beginner.levels[skill] = 1;
    }
    for (const std::string& label : {"DR", "7I", "PW"}) {
        const float top = crit_share(label, golfer_profile{}, 2000);
        const float low = crit_share(label, beginner, 2000);
        std::printf("%s crits: %.1f%% at the top level, %.1f%% at level one\n", label.c_str(), 100.0f * top, 100.0f * low);
        CHECK(top >= 0.02f);
        CHECK(top <= 0.15f);
        CHECK(low < top);
    }
}

TEST_CASE("my shot's number pops up over my ball as I strike it, rising above it on screen, and is gone after its time") {
    game_state state = started_hole();
    address_looking_down(state);
    start_swing_on_time(state);
    swing_through(state);
    REQUIRE(state.mode == game_mode::following_shot);
    REQUIRE(state.shot_number.has_value());
    for (int frame = 0; frame < 10; ++frame) {
        update_game(state, game_input{}, 0.03f);
    }

    const render_data data = frame_for(state);
    REQUIRE(data.shot_number.has_value());
    CHECK(data.shot_number->position == data.ball_position);
    for (const float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
        const glm::mat4 view_proj = view_proj_of(data, aspect);
        const std::optional<glm::vec2> ball = project_to_screen(view_proj, data.ball_position);
        REQUIRE(ball.has_value());
        overlay_batch batch;
        batch.grid = overlay_grid{static_cast<int>(360.0f * aspect), 360};
        draw_shot_number(batch, *load_text_assets(asset_root()), *data.shot_number, view_proj);
        REQUIRE(!batch.vertices.empty());
        CHECK(batch.truncated_text_count == 0U);
        float lowest = 1.0e9f;
        float left = 1.0e9f;
        float right = -1.0e9f;
        for (const overlay_vertex& vertex : batch.vertices) {
            lowest = std::min(lowest, vertex.position.y);
            left = std::min(left, vertex.position.x);
            right = std::max(right, vertex.position.x);
        }
        CHECK(lowest > ball->y);
        CHECK(left < ball->x);
        CHECK(right > ball->x);
    }

    for (float age = 0.0f; age < state.tuning.shot_numbers.lifetime_seconds; age += 0.03f) {
        update_game(state, game_input{}, 0.03f);
    }
    CHECK(!state.shot_number);
    CHECK(!frame_for(state).shot_number);
}

TEST_CASE("a drive's number is drawn a font size bigger than a pitch's, a crit's bigger still, biggest as it pops") {
    game_state state = started_hole();
    const text_assets text = *load_text_assets(asset_root());
    const auto scale = [&](const float meters, const bool crit, const float age) -> int {
        state.shot_number = shot_number_popup{shot_number{meters, crit}, age};
        const render_shot_number number = *frame_for(state).shot_number;
        overlay_batch batch;
        batch.grid = overlay_grid{640, 360};
        draw_shot_number(batch, text, number, glm::mat4(1.0f));
        CHECK(batch.truncated_text_count == 0U);
        float low = 1.0e9f;
        float high = -1.0e9f;
        for (const overlay_vertex& vertex : batch.vertices) {
            low = std::min(low, vertex.position.y);
            high = std::max(high, vertex.position.y);
        }
        // Rows of target pixels, edge included, over the font's own.
        return static_cast<int>(std::lround((high - low) * 180.0f)) / (text.font.height + 2);
    };
    const float settled = state.tuning.shot_numbers.crit_pop_seconds + 0.1f;
    CHECK(scale(60.0f, false, settled) >= 2);
    CHECK(scale(260.0f, false, settled) == scale(60.0f, false, settled) + 1);
    CHECK(scale(60.0f, true, settled) > scale(60.0f, false, settled));
    CHECK(scale(260.0f, true, settled) > scale(260.0f, false, settled));
    CHECK(scale(260.0f, true, 0.0f) > scale(260.0f, true, settled));
    CHECK(frame_for(state).shot_number->label == "260");
}
