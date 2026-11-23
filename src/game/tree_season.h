#pragma once

// The trees' year (assets/play/tuning/trees.json): each species' leaves come
// out, turn and fall on its own days, as Danish phenology has them, so a
// course's date (its course_sky, which never changes) sets how much of each
// crown is in leaf, how far it has turned, how many withered leaves it keeps
// through winter (beech and oak: marcescence), and how much a ball is slowed
// through it: a bare crown is twigs, bare_crown_density of a leafy one, and
// its withered leaves slow a ball as live ones do. Plain arithmetic, so the
// server builds the same crowns.

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
    // The share of a full crown's leaves it keeps on, withered, when the rest
    // fall (most on young trees, hedges and low in the crown).
    float withered_share = 0.0f;
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
    // The share of the withered leaves still on at leaf-out; the rest fall
    // through the winter, these as the new leaves come.
    float withered_left_at_leaf_out = 0.0f;
    // The share of a full crown's leaves a broadleaf drops a day in summer.
    float summer_leaf_fall_per_day = 0.0f;
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

// The share of a full crown's leaves it holds withered on `day` (1..365):
// they wither as the others fall, thin through winter to
// `withered_left_at_leaf_out` and fall as the new leaves come out.
float withered_share(const tree_tuning& trees, const tree_species& species, float day);

// The share of a full crown's leaves on it on `day`, live and withered.
float crown_leaves(const tree_tuning& trees, const tree_species& species, float day);

// The share of a full crown's leaves falling on `day`, per day: as its leaves
// fall in autumn, its withered ones through winter and at leaf-out, a few in
// summer; none from an evergreen. Of those, `withered` (when given) is set to
// the share that are withered leaves.
float leaves_falling(const tree_tuning& trees, const tree_species& species, float day, float* withered = nullptr);

// How much of a leafy crown's drag a crown with `leaves` of a full crown's
// leaves on it (crown_leaves) has.
float crown_density(const tree_tuning& trees, float leaves);

// The index of the species named `name`, if any.
std::optional<std::uint8_t> species_index(const tree_tuning& trees, const std::string& name);
