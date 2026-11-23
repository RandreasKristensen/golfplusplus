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

const year_season& season_on(const tree_tuning& trees, const int day) {
    const year_season* found = &trees.seasons.back();
    for (const year_season& season : trees.seasons) {
        if (season.start_day <= day) { found = &season; }
    }
    return *found;
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

float withered_share(const tree_tuning& trees, const tree_species& species, const float day) {
    if (species.evergreen || species.withered_share <= 0.0f) {
        return 0.0f;
    }
    constexpr float year = 365.0f;
    const float kept = species.withered_share;
    const float left = trees.withered_left_at_leaf_out;
    if (day >= species.turned_day) {
        // Withering as the others fall, then thinning into the winter.
        const float winter = std::max(species.leaf_out_day + year - species.bare_day, 1.0f);
        const float thinned = day > species.bare_day ? (day - species.bare_day) / winter : 0.0f;
        return kept * ramp(species.turned_day, species.bare_day, day) * (1.0f - (1.0f - left) * thinned);
    }
    if (day < species.leaf_out_day) {
        const float winter = std::max(species.leaf_out_day + year - species.bare_day, 1.0f);
        const float thinned = (day + year - species.bare_day) / winter;
        return kept * (1.0f - (1.0f - left) * std::clamp(thinned, 0.0f, 1.0f));
    }
    // Falling as the new leaves come out.
    return kept * left * (1.0f - ramp(species.leaf_out_day, species.full_leaf_day, day));
}

float crown_leaves(const tree_tuning& trees, const tree_species& species, const float day) {
    return std::min(foliage_share(species, day) + withered_share(trees, species, day), 1.0f);
}

float leaves_falling(const tree_tuning& trees, const tree_species& species, const float day, float* withered) {
    if (withered != nullptr) {
        *withered = 0.0f;
    }
    if (species.evergreen) {
        return 0.0f;
    }
    // A day's change, across midnight of the new year too.
    const auto wrapped = [](const float d) { return d < 1.0f ? d + 365.0f : (d > 365.0f ? d - 365.0f : d); };
    const float before = wrapped(day - 0.5f);
    const float after = wrapped(day + 0.5f);
    const float lost = std::max(crown_leaves(trees, species, before) - crown_leaves(trees, species, after), 0.0f);
    const float withered_lost = std::max(withered_share(trees, species, before) - withered_share(trees, species, after), 0.0f);
    const bool summer = day >= species.full_leaf_day && day < species.turned_day;
    const float falling = lost + (summer ? trees.summer_leaf_fall_per_day : 0.0f);
    if (withered != nullptr && falling > 0.0f) {
        *withered = std::min(withered_lost / falling, 1.0f);
    }
    return falling;
}

float crown_density(const tree_tuning& trees, const float leaves) {
    const float leaf = std::clamp(leaves, 0.0f, 1.0f);
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
