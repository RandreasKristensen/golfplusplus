#include "game/shot_ability.h"

#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <utility>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
// The share of the way from `start` to `full`.
float ramp(const float value, const float start, const float full) {
    return full > start ? clamp01((value - start) / (full - start)) : (value >= full ? 1.0f : 0.0f);
}
}

shape_weights classify_setup(const shot_setup& setup, const club_definition& club, const ball_lie lie,
                             const golfer_tuning& tuning) {
    shape_weights shape;
    if (club.club_class == "putter") {
        shape.putt = 1.0f;
        return shape;
    }
    const float open_degrees = glm::degrees(setup.face_open);
    const float stock_open_degrees = lie == ball_lie::bunker ? club.spec.sand_face_open_degrees : 0.0f;
    shape.flop = club.club_class == "wedges" && lie != ball_lie::bunker
        ? ramp(open_degrees, tuning.flop_start_degrees, tuning.flop_full_degrees)
        : 0.0f;
    const float curve = open_degrees - stock_open_degrees;
    shape.fade = ramp(curve, 0.0f, tuning.curve_full_degrees) * (1.0f - shape.flop);
    shape.draw = ramp(-curve, 0.0f, tuning.curve_full_degrees);
    const float moved = setup.ball_position - club.spec.stock_ball_position;
    shape.punch = ramp(-moved, 0.0f, tuning.ball_full);
    shape.high = ramp(moved, 0.0f, tuning.ball_full);
    shape.bunker = lie == ball_lie::bunker ? 1.0f : 0.0f;
    return shape;
}

setup_word setup_word_for(const shape_weights& shape, const golfer_tuning& tuning) {
    setup_word word;
    const float threshold = tuning.word_threshold;
    if (shape.punch >= threshold) {
        word.height = text_hud_setup_word_punch;
    } else if (shape.high >= threshold) {
        word.height = text_hud_setup_word_high;
    }
    if (shape.flop >= threshold) {
        word.curve = text_hud_setup_word_flop;
    } else if (shape.fade >= threshold) {
        word.curve = text_hud_setup_word_fade;
    } else if (shape.draw >= threshold) {
        word.curve = text_hud_setup_word_draw;
    }
    return word;
}

shot_ability ability_for(const shot_setup& setup, const club_definition& club, const ball_lie lie,
                         const golfer_profile& golfer, const game_tuning& tuning) {
    const golfer_tuning& numbers = tuning.golfer;
    const shape_weights shape = classify_setup(setup, club, lie, numbers);
    const std::vector<std::pair<std::string, float>> skills{
        {"club." + club.club_class, 1.0f}, {"shape.fade", shape.fade},     {"shape.draw", shape.draw},
        {"shape.punch", shape.punch},      {"shape.high", shape.high},     {"shape.flop", shape.flop},
        {"shape.bunker", shape.bunker},    {"shape.putt", shape.putt}};
    float weighted = 0.0f;
    float total = 0.0f;
    for (const auto& [skill, weight] : skills) {
        weighted += weight * static_cast<float>(skill_level(golfer, skill));
        total += weight;
    }
    const float level = weighted / total;
    const float below_top = ramp(static_cast<float>(golfer_top_level) - level, 0.0f, static_cast<float>(golfer_top_level - 1));
    float scale = 1.0f + (numbers.spread_at_level_one - 1.0f) * below_top;

    // Playing the natural shape: a fade or draw as much as it is one, a
    // neutral golfer's as much as it is straight. Putts have no shape.
    const float natural = golfer.natural == natural_shape::fade ? shape.fade
        : golfer.natural == natural_shape::draw                 ? shape.draw
                                                                : 1.0f - std::max(shape.fade, shape.draw);
    scale *= 1.0f - (1.0f - numbers.natural_shape_spread) * natural * (1.0f - shape.putt);
    scale *= golfer.spread_scale;

    const setup_limits limits = setup_limits_for(club, tuning.setup);
    const float over = limits.backswing_max > 1.0f ? ramp(setup.backswing, 1.0f, limits.backswing_max) : 0.0f;
    const float overswing = 1.0f + (numbers.overswing_spread - 1.0f) * over;

    shot_ability ability;
    ability.limits = limits;
    ability.spread.speed_share = numbers.speed_share_spread * scale;
    ability.spread.face = glm::radians(numbers.face_spread_degrees) * scale * overswing;
    ability.spread.path = glm::radians(numbers.path_spread_degrees) * scale;
    ability.spread.strike_toe = numbers.strike_toe_spread_meters * scale * overswing;
    ability.spread.strike_high = numbers.strike_high_spread_meters * scale * overswing;
    return ability;
}

float natural_face_bias(const club_definition& club, const natural_shape natural, const golfer_tuning& tuning) {
    if (club.club_class == "putter" || natural == natural_shape::neutral) {
        return 0.0f;
    }
    const float bias = glm::radians(tuning.natural_face_bias_degrees);
    return natural == natural_shape::fade ? bias : -bias;
}
