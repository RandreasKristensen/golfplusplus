#pragma once

// Who swings: their level in each skill, the shape their shots lean to and
// their hand. Every level reads golfer_top_level until the skill plan fills
// them (skill_level). Pure.

#include "game/shot_setup.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

inline constexpr int golfer_top_level = 99;

// The trait a golfer picks: the shape their stock shot leans to, played
// a little better (shot_ability.h). In the setting's order.
enum class natural_shape : std::uint8_t {
    draw,
    neutral,
    fade
};

struct golfer_profile {
    std::map<std::string, int> levels;  // by skill id; a skill missing here is at golfer_top_level
    natural_shape natural = natural_shape::neutral;
    handedness hand = handedness::right;
    // Every spread multiplied by this: items' effects on shot.spread
    // (effects.h effective_value), 1 without any.
    float spread_scale = 1.0f;
};

int skill_level(const golfer_profile& golfer, const std::string& skill);

// A natural shape by its number before its golfer picked one (an online
// player row's).
inline constexpr std::uint8_t natural_shape_unset = 255;

// The natural shape a setting's value (0, 1, 2) or a save's names, or nullopt.
std::optional<natural_shape> natural_shape_from_index(int index);
int natural_shape_index(natural_shape shape);
std::optional<natural_shape> natural_shape_from_name(const std::string& name);
const char* natural_shape_name(natural_shape shape);
