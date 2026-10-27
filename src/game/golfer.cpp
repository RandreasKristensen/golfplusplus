#include "game/golfer.h"

namespace {
constexpr const char* shape_names[] = {"draw", "neutral", "fade"};
}

int skill_level(const golfer_profile& golfer, const std::string& skill) {
    const auto found = golfer.levels.find(skill);
    return found == golfer.levels.end() ? golfer_top_level : found->second;
}

std::optional<natural_shape> natural_shape_from_index(const int index) {
    if (index < 0 || index > 2) {
        return std::nullopt;
    }
    return static_cast<natural_shape>(index);
}

int natural_shape_index(const natural_shape shape) {
    return static_cast<int>(shape);
}

std::optional<natural_shape> natural_shape_from_name(const std::string& name) {
    for (int i = 0; i < 3; ++i) {
        if (name == shape_names[i]) {
            return static_cast<natural_shape>(i);
        }
    }
    return std::nullopt;
}

const char* natural_shape_name(const natural_shape shape) {
    return shape_names[natural_shape_index(shape)];
}
