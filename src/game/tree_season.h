#pragma once

// The trees' year (assets/play/tuning/trees.json): each species' leaves come
// out, turn and fall on its own days, as Danish phenology has them, so a
// course's date (its course_sky, which never changes) sets how much of each
// crown is in leaf, how far it has turned, and how much a ball is slowed
// through it: a bare crown is twigs, bare_crown_density of a leafy one.
// Plain arithmetic, so the server builds the same crowns.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct tree_species {
    std::string name;
    bool evergreen = false;
    // Days of the year (1 = 1 January): the buds break, the crown is full,
    // the leaves have turned and begin to fall, and the crown is bare.
    float leaf_out_day = 0.0f;
    float full_leaf_day = 0.0f;
    float turned_day = 0.0f;
    float bare_day = 0.0f;
};

// A named stretch of the year, from its first day to the next season's: the
// words the tee book gives a course's date, set against the trees' own days
// (spring begins before the first leaf, autumn when the default species begins
// to colour, winter when it is bare).
struct year_season {
    std::string name;  // text key "hub.season.<name>"
    int start_day = 0;  // 1..365, ascending through the list; the last wraps to the first
};

struct tree_tuning {
    std::vector<year_season> seasons;
    std::vector<tree_species> species;
    float bare_crown_density = 0.0f;
};

// A world without a course (the hub) stands on this day: midsummer, in full leaf.
inline constexpr int midsummer_day_of_year = 172;

// The day of the year (1..365) of `month` (1..12) and `day`, in a common year.
int day_of_year(int month, int day);

// The season `day` (1..365) is in: the last to start by it, or the year's last
// before the first starts. `trees.seasons` must not be empty.
const year_season& season_on(const tree_tuning& trees, int day);

// The share of a crown in leaf on `day` (1..365): 0 bare .. 1 full.
float foliage_share(const tree_species& species, float day);

// How far its leaves have turned towards their autumn colour on `day`: 0 green
// .. 1 turned, from a month before turned_day; none for an evergreen.
float turned_share(const tree_species& species, float day);

// How fresh its leaves still are on `day`: 1 when they break bud .. 0 a
// month after the crown is full, their spring green darkened to summer's;
// none for an evergreen.
float fresh_share(const tree_species& species, float day);

// How much of a leafy crown's drag a crown `foliage` in leaf has.
float crown_density(const tree_tuning& trees, float foliage);

// The index of the species named `name`, if any.
std::optional<std::uint8_t> species_index(const tree_tuning& trees, const std::string& name);
