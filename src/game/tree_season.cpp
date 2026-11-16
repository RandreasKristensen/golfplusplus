#include "game/tree_season.h"

#include <algorithm>
#include <array>

namespace {
// A month before the leaves have turned they begin to colour.
constexpr float turning_days = 30.0f;

float ramp(const float from, const float to, const float day) {
    if (to <= from) {
        return day >= to ? 1.0f : 0.0f;
    }
    return std::clamp((day - from) / (to - from), 0.0f, 1.0f);
}
}

int day_of_year(const int month, const int day) {
    constexpr std::array<int, 12> before = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    const int m = std::clamp(month, 1, 12);
    return before[static_cast<std::size_t>(m - 1)] + std::clamp(day, 1, 31);
}

float foliage_share(const tree_species& species, const float day) {
    if (species.evergreen) {
        return 1.0f;
    }
    return ramp(species.leaf_out_day, species.full_leaf_day, day) * (1.0f - ramp(species.turned_day, species.bare_day, day));
}

float turned_share(const tree_species& species, const float day) {
    if (species.evergreen) {
        return 0.0f;
    }
    return ramp(species.turned_day - turning_days, species.turned_day, day);
}

float fresh_share(const tree_species& species, const float day) {
    if (species.evergreen) {
        return 0.0f;
    }
    return 1.0f - ramp(species.full_leaf_day, species.full_leaf_day + turning_days, day);
}

float crown_density(const tree_tuning& trees, const float foliage) {
    const float leaf = std::clamp(foliage, 0.0f, 1.0f);
    return trees.bare_crown_density + (1.0f - trees.bare_crown_density) * leaf;
}

std::optional<std::uint8_t> species_index(const tree_tuning& trees, const std::string& name) {
    for (std::size_t i = 0; i < trees.species.size(); ++i) {
        if (trees.species[i].name == name) {
            return static_cast<std::uint8_t>(i);
        }
    }
    return std::nullopt;
}
