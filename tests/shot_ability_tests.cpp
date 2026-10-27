#include "doctest.h"

#include "game/mode_dispatch.h"
#include "game/save_data.h"
#include "game/shot_ability.h"
#include "game/shot_dice.h"
#include "game/shot_simulation.h"
#include "game/text_ids.h"
#include "physics/ball_physics.h"
#include "physics/flight_model.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
const club_definition& club_labelled(const std::string& label) {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    return *std::find_if(clubs.begin(), clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

// Where a ball launched from level ground, in still air at sea level and
// 15 C, comes back down to it.
glm::vec3 landing_of(const launch_conditions& launch) {
    const game_tuning& tuning = shipped_content().tuning;
    const air_state air = air_at(tuning.physics.air, 0.0f, 15.0f);
    ball_state ball = launched_ball(glm::vec3(0.0f), tuning.scale.ball_physics_radius_meters, launch);
    for (int step = 0; step < 120 * 30; ++step) {
        const ball_state next = step_ball_flight(ball, wind_state{}, air, false, 1.0f / 120.0f, tuning.physics);
        if (next.position.y < 0.0f) {
            const float share = ball.position.y / (ball.position.y - next.position.y);
            return ball.position + (next.position - ball.position) * share;
        }
        ball = next;
    }
    return ball.position;
}

// The landings of `count` shots of `setup` off `lie` by `golfer`, each with its own dice.
std::vector<glm::vec3> landings(const club_definition& club, const shot_setup& setup, const ball_lie lie,
                                const golfer_profile& golfer, const int count) {
    const game_tuning& tuning = shipped_content().tuning;
    const shot_ability ability = ability_for(setup, club, lie, golfer, tuning);
    std::vector<glm::vec3> out;
    for (int i = 0; i < count; ++i) {
        const swing_errors errors = rolled_errors(round_shot_seed(12345, i), ability.spread);
        out.push_back(landing_of(club_strike(club, setup, ball_on_lie{lie, {}, 0.0f}, tuning, errors).launch));
    }
    return out;
}

// How wide the middle `share` of `values` spreads.
float window(std::vector<float> values, const float share) {
    std::sort(values.begin(), values.end());
    const float cut = 0.5f * (1.0f - share) * static_cast<float>(values.size() - 1);
    return values[values.size() - 1 - static_cast<std::size_t>(cut)] - values[static_cast<std::size_t>(cut)];
}

// Across the line (x, left of an aim along +z) and along it.
float across_window(const std::vector<glm::vec3>& points) {
    std::vector<float> values;
    for (const glm::vec3& point : points) {
        values.push_back(point.x);
    }
    return window(values, 0.9f);
}

float along_window(const std::vector<glm::vec3>& points) {
    std::vector<float> values;
    for (const glm::vec3& point : points) {
        values.push_back(point.z);
    }
    return window(values, 0.9f);
}
}

TEST_CASE("dice: uniforms in [0, 1), normals centred with deviation 1, the same seed the same roll") {
    double sum = 0.0;
    double squares = 0.0;
    constexpr int count = 4000;
    for (int i = 0; i < count; ++i) {
        const float uniform = dice_uniform(99, shot_strike_step, static_cast<std::uint64_t>(i));
        CHECK(uniform >= 0.0f);
        CHECK(uniform < 1.0f);
        const float normal = dice_normal(7, i, 0);
        sum += normal;
        squares += normal * normal;
    }
    const double mean = sum / count;
    CHECK(std::abs(mean) < 0.06);
    CHECK(std::abs(std::sqrt(squares / count - mean * mean) - 1.0) < 0.05);

    const swing_errors spread{0.01f, 0.02f, 0.03f, 0.004f, 0.005f};
    const swing_errors a = rolled_errors(42, spread);
    const swing_errors b = rolled_errors(42, spread);
    CHECK(std::memcmp(&a, &b, sizeof(a)) == 0);
    const swing_errors c = rolled_errors(43, spread);
    CHECK(a.face != c.face);
    CHECK(round_shot_seed(5, 1) != round_shot_seed(5, 2));
}

TEST_CASE("at 99 a full driver lands in a window 15 to 20 m wide, and a full wedge holds a green") {
    const golfer_profile golfer;
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& driver = club_labelled("DR");
    const std::vector<glm::vec3> drives =
        landings(driver, stock_swing(driver, tuning, 1.0f, ball_lie::tee), ball_lie::tee, golfer, 400);
    const float drive_window = across_window(drives);
    CHECK(drive_window >= 15.0f);
    CHECK(drive_window <= 20.0f);

    const club_definition& wedge = club_labelled("PW");
    const std::vector<glm::vec3> pitches =
        landings(wedge, stock_swing(wedge, tuning, 1.0f, ball_lie::fairway), ball_lie::fairway, golfer, 400);
    // A green is about 25 m across and 30 m deep.
    CHECK(across_window(pitches) < 12.0f);
    CHECK(along_window(pitches) < 15.0f);
    // Never perfect: the dice always move it a little.
    CHECK(across_window(pitches) > 1.0f);
}

TEST_CASE("playing the natural shape narrows the spread; overswing widens the face and strike") {
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& seven = club_labelled("7I");
    shot_setup fade = stock_swing(seven, tuning);
    fade.face_open = glm::radians(tuning.golfer.curve_full_degrees);

    golfer_profile fader;
    fader.natural = natural_shape::fade;
    golfer_profile drawer;
    drawer.natural = natural_shape::draw;
    const shot_ability natural = ability_for(fade, seven, ball_lie::fairway, fader, tuning);
    const shot_ability unnatural = ability_for(fade, seven, ball_lie::fairway, drawer, tuning);
    CHECK(near(natural.spread.face, unnatural.spread.face * tuning.golfer.natural_shape_spread, 1.0e-6f));
    CHECK(near(natural.spread.speed_share, unnatural.spread.speed_share * tuning.golfer.natural_shape_spread, 1.0e-6f));

    // A neutral golfer plays straight shots best.
    golfer_profile neutral;
    const shot_ability straight = ability_for(stock_swing(seven, tuning), seven, ball_lie::fairway, neutral, tuning);
    const shot_ability straight_by_fader = ability_for(stock_swing(seven, tuning), seven, ball_lie::fairway, fader, tuning);
    CHECK(straight.spread.face < straight_by_fader.spread.face);

    shot_setup over = stock_swing(seven, tuning);
    over.backswing = tuning.setup.overswing_max;
    const shot_ability full = ability_for(stock_swing(seven, tuning), seven, ball_lie::fairway, neutral, tuning);
    const shot_ability overswung = ability_for(over, seven, ball_lie::fairway, neutral, tuning);
    CHECK(near(overswung.spread.face, full.spread.face * tuning.golfer.overswing_spread, 1.0e-6f));
    CHECK(near(overswung.spread.strike_toe, full.spread.strike_toe * tuning.golfer.overswing_spread, 1.0e-6f));
    CHECK(overswung.spread.path == full.spread.path);

    // Below the top level everything spreads wider; an item's effect scales it.
    golfer_profile beginner;
    beginner.levels["club.short_irons"] = 1;
    const shot_ability raw = ability_for(stock_swing(seven, tuning), seven, ball_lie::fairway, beginner, tuning);
    CHECK(near(raw.spread.face, full.spread.face * tuning.golfer.spread_at_level_one, 1.0e-6f));
    golfer_profile steadied;
    steadied.spread_scale = 0.5f;
    CHECK(near(ability_for(stock_swing(seven, tuning), seven, ball_lie::fairway, steadied, tuning).spread.path,
               full.spread.path * 0.5f, 1.0e-6f));
}

TEST_CASE("a natural shape leans the stock face that way, a putter's never") {
    const game_tuning& tuning = shipped_content().tuning;
    const club_definition& seven = club_labelled("7I");
    CHECK(natural_face_bias(seven, natural_shape::fade, tuning.golfer) > 0.0f);
    CHECK(natural_face_bias(seven, natural_shape::draw, tuning.golfer) < 0.0f);
    CHECK(natural_face_bias(seven, natural_shape::neutral, tuning.golfer) == 0.0f);
    CHECK(natural_face_bias(club_labelled("P"), natural_shape::fade, tuning.golfer) == 0.0f);

    // Stepping up to the ball offline, the stock setup leans to the save's shape, yet still reads as stock.
    game_state state = started_hole();
    state.save.natural = natural_shape::draw;
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    const club_definition& club = state.clubs[state.selected_club];
    CHECK(state.address.setup.face_open < 0.0f);
    const setup_word word = setup_word_for(
        classify_setup(state.address.setup, club, lie_at(state.area, state.ball->position), tuning.golfer), tuning.golfer);
    CHECK(word.height == nullptr);
    CHECK(word.curve == nullptr);
}

TEST_CASE("the setup word names what was built, the same for either hand") {
    const game_tuning& tuning = shipped_content().tuning;
    const golfer_tuning& numbers = tuning.golfer;
    const club_definition& seven = club_labelled("7I");
    const auto word_of = [&](const shot_setup& setup, const club_definition& club, const ball_lie lie) {
        return setup_word_for(classify_setup(setup, club, lie, numbers), numbers);
    };
    const auto same = [](const char* a, const char* b) { return a == b || (a && b && std::string(a) == b); };

    const setup_word stock = word_of(stock_swing(seven, tuning), seven, ball_lie::fairway);
    CHECK(stock.height == nullptr);
    CHECK(stock.curve == nullptr);

    for (const handedness hand : {handedness::right, handedness::left}) {
        shot_setup fade = stock_swing(seven, tuning);
        fade.hand = hand;
        fade.face_open = glm::radians(numbers.curve_full_degrees);
        CHECK(same(word_of(fade, seven, ball_lie::fairway).curve, text_hud_setup_word_fade));
        shot_setup draw = fade;
        draw.face_open = -fade.face_open;
        CHECK(same(word_of(draw, seven, ball_lie::fairway).curve, text_hud_setup_word_draw));
    }

    shot_setup punch_fade = stock_swing(seven, tuning);
    punch_fade.ball_position = seven.spec.stock_ball_position - numbers.ball_full;
    punch_fade.face_open = glm::radians(numbers.curve_full_degrees);
    const setup_word both = word_of(punch_fade, seven, ball_lie::fairway);
    CHECK(same(both.height, text_hud_setup_word_punch));
    CHECK(same(both.curve, text_hud_setup_word_fade));

    shot_setup high = stock_swing(seven, tuning);
    high.ball_position = seven.spec.stock_ball_position + numbers.ball_full;
    CHECK(same(word_of(high, seven, ball_lie::fairway).height, text_hud_setup_word_high));

    // A wedge's face opened wide is a flop, not a fade; a 7-iron's never is.
    const club_definition& lob = club_labelled("LW");
    shot_setup flop = stock_swing(lob, tuning);
    flop.face_open = glm::radians(numbers.flop_full_degrees);
    CHECK(same(word_of(flop, lob, ball_lie::fairway).curve, text_hud_setup_word_flop));
    shot_setup wide = stock_swing(seven, tuning);
    wide.face_open = glm::radians(numbers.flop_full_degrees);
    CHECK(same(word_of(wide, seven, ball_lie::fairway).curve, text_hud_setup_word_fade));

    // From sand the wedge's stock open face is stock, not a fade.
    const club_definition& sand_wedge = club_labelled("SW");
    const shot_setup splash = stock_swing(sand_wedge, tuning, 1.0f, ball_lie::bunker);
    CHECK(splash.face_open > 0.0f);
    CHECK(word_of(splash, sand_wedge, ball_lie::bunker).curve == nullptr);

    // A putt is only a putt.
    const club_definition& putter = club_labelled("P");
    shot_setup putt = stock_swing(putter, tuning);
    putt.face_open = glm::radians(5.0f);
    const shape_weights putting = classify_setup(putt, putter, ball_lie::green, numbers);
    CHECK(putting.putt == 1.0f);
    CHECK(putting.fade == 0.0f);
}

TEST_CASE("a struck shot replays bit-identically from its seed, and other dice hit it elsewhere") {
    game_state state = started_hole();
    const shot_course course = current_shot_course(state);
    shot_setup setup = stock_swing(state.clubs[state.selected_club], state.tuning, 1.0f, lie_at(state.area, state.ball->position),
                                   state.player.yaw);
    setup.ball_start = state.ball->position;
    const golfer_profile golfer;
    const shot_result first = simulate_struck_shot(shot_record{setup, 77}, golfer, course, state.tuning, state.clubs);
    const shot_result again = simulate_struck_shot(shot_record{setup, 77}, golfer, course, state.tuning, state.clubs);
    const shot_result other = simulate_struck_shot(shot_record{setup, 78}, golfer, course, state.tuning, state.clubs);
    CHECK(first.rest_position == again.rest_position);
    CHECK(first.trajectory == again.trajectory);
    CHECK(first.rest_position != other.rest_position);
}

TEST_CASE("offline each shot of a round rolls the round's next dice") {
    game_state state = started_hole();
    const int before = state.shots_hit;
    const std::uint64_t first = next_shot_seed(state);
    hit_selected_club(state);
    CHECK(state.shots_hit == before + 1);
    CHECK(next_shot_seed(state) != first);
}

TEST_CASE("the save keeps the natural shape, and a save without one has none until it is picked") {
    save_data save;
    CHECK(!save.natural);
    save.natural = natural_shape::fade;
    const std::optional<save_data> back = parse_save_data(save_data_to_json(save));
    REQUIRE(back.has_value());
    REQUIRE(back->natural.has_value());
    CHECK(*back->natural == natural_shape::fade);
    CHECK(!parse_save_data(save_data_to_json(save_data{}))->natural);

    game_state state = started_hole();
    CHECK(my_golfer(state).natural == natural_shape::neutral);
    state.save.natural = natural_shape::draw;
    CHECK(my_golfer(state).natural == natural_shape::draw);
}
