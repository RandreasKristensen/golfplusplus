#include "doctest.h"

#include "game/area_solids.h"
#include "game/content_files.h"
#include "game/hole_data.h"
#include "game/play_area.h"
#include "game/tree_season.h"
#include "test_support.h"

namespace {
const tree_species& named(const tree_tuning& trees, const char* name) {
    return trees.species[*species_index(trees, name)];
}
}

TEST_CASE("a beech is bare in winter, in leaf in summer and turned in late October, a spruce green all year") {
    const tree_tuning& trees = shipped_content().tuning.trees;
    REQUIRE(species_index(trees, "beech").has_value());
    REQUIRE(species_index(trees, "spruce").has_value());
    const tree_species& beech = named(trees, "beech");
    const tree_species& spruce = named(trees, "spruce");
    const float january = static_cast<float>(day_of_year(1, 15));
    const float july = static_cast<float>(day_of_year(7, 1));
    const float late_october = static_cast<float>(day_of_year(10, 20));
    CHECK(foliage_share(beech, january) == 0.0f);
    CHECK(foliage_share(beech, july) == 1.0f);
    CHECK(turned_share(beech, july) == 0.0f);
    CHECK(turned_share(beech, late_october) > 0.8f);
    CHECK(foliage_share(beech, late_october) > 0.5f);
    CHECK(foliage_share(beech, static_cast<float>(day_of_year(12, 1))) == 0.0f);
    CHECK(foliage_share(spruce, january) == 1.0f);
    CHECK(turned_share(spruce, late_october) == 0.0f);
    // A birch breaks bud before a beech, an oak after.
    const float early_may = static_cast<float>(day_of_year(5, 5));
    CHECK(foliage_share(named(trees, "birch"), early_may) > foliage_share(beech, early_may));
    CHECK(foliage_share(named(trees, "oak"), early_may) < foliage_share(beech, early_may));
}

TEST_CASE("a bare crown slows a ball less than a leafy one, but more than nothing") {
    const tree_tuning& trees = shipped_content().tuning.trees;
    CHECK(crown_density(trees, 1.0f) == 1.0f);
    CHECK(crown_density(trees, 0.0f) > 0.0f);
    CHECK(crown_density(trees, 0.0f) < crown_density(trees, 0.5f));
    CHECK(crown_density(trees, 0.5f) < 1.0f);
}

TEST_CASE("a crown out of leaf stands as thinner foliage, and a bush as thick as the tuning says") {
    const game_tuning& tuning = shipped_content().tuning;
    play_area area;
    tree_instance leafy;
    leafy.position = glm::vec3(0.0f, 0.0f, 0.0f);
    leafy.shape = tree_shape{0.3f, 3.0f, 2.0f, 4.0f};
    tree_instance bare = leafy;
    bare.position.x = 20.0f;
    bare.shape.foliage = 0.0f;
    tree_instance bush;
    bush.position.x = 40.0f;
    bush.shape = tree_shape{0.0f, 0.0f, 1.5f, 1.5f, tree_kind::bush};
    area.trees = {leafy, bare, bush};
    std::vector<float> densities;
    for (const solid& thing : standing_solids(area, tuning)) {
        if (thing.porous) {
            densities.push_back(thing.density);
        }
    }
    REQUIRE(densities.size() == 3U);
    CHECK(densities[0] == 1.0f);
    CHECK(densities[1] < densities[0]);
    CHECK(densities[1] == crown_density(tuning.trees, 0.0f));
    CHECK(densities[2] == tuning.solids.bush_density);
}

TEST_CASE("the days of the year count from the first of January") {
    CHECK(day_of_year(1, 1) == 1);
    CHECK(day_of_year(2, 1) == 32);
    CHECK(day_of_year(6, 21) == 172);
    CHECK(day_of_year(12, 31) == 365);
    CHECK(midsummer_day_of_year == day_of_year(6, 21));
}

TEST_CASE("every shipped tree and bush is of a species the tuning knows") {
    const game_content& content = shipped_content();
    for (const course_definition& course : content.courses) {
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(content.asset_root, course, i));
            REQUIRE(hole.has_value());
            for (const tree_instance& tree : hole->trees) {
                CHECK((tree.species.empty() || species_index(content.tuning.trees, tree.species).has_value()));
            }
        }
    }
}

TEST_CASE("seasons change on their start days and wrap through the new year") {
    const tree_tuning& trees = shipped_content().tuning.trees;
    const auto name_on = [&](const int month, const int day) { return season_on(trees, day_of_year(month, day)).name; };
    CHECK(name_on(3, 19) == "winter");
    CHECK(name_on(3, 20) == "spring");
    CHECK(name_on(5, 31) == "spring");
    CHECK(name_on(6, 1) == "early_summer");
    CHECK(name_on(6, 20) == "early_summer");
    CHECK(name_on(6, 21) == "midsummer");
    CHECK(name_on(7, 31) == "midsummer");
    CHECK(name_on(8, 1) == "late_summer");
    CHECK(name_on(9, 21) == "late_summer");
    CHECK(name_on(9, 22) == "autumn");
    CHECK(name_on(11, 13) == "autumn");
    CHECK(name_on(11, 14) == "winter");
    CHECK(name_on(12, 31) == "winter");
    CHECK(name_on(1, 1) == "winter");
    // The hub's own day, midsummer, is the season of full leaf.
    CHECK(season_on(trees, midsummer_day_of_year).name == "midsummer");
}

TEST_CASE("the seasons agree with the trees: no leaf before spring, full by summer, the default species turning in autumn and bare in winter") {
    const tree_tuning& trees = shipped_content().tuning.trees;
    const auto start = [&](const char* name) {
        for (const year_season& season : trees.seasons) { if (season.name == name) { return static_cast<float>(season.start_day); } }
        return -1.0f;
    };
    for (const char* name : {"spring", "early_summer", "midsummer", "late_summer", "autumn", "winter"}) { REQUIRE(start(name) > 0.0f); }
    for (const tree_species& species : trees.species) {
        if (species.evergreen) { continue; }
        CHECK(foliage_share(species, start("spring") - 1.0f) == 0.0f);
        CHECK(foliage_share(species, start("early_summer")) == 1.0f);
    }
    const tree_species& main = trees.species.front();
    CHECK(turned_share(main, start("autumn") - 1.0f) == 0.0f);
    CHECK(turned_share(main, start("autumn") + 14.0f) > 0.0f);
    CHECK(foliage_share(main, start("winter")) == 0.0f);
    CHECK(foliage_share(main, start("winter") - 1.0f) > 0.0f);
}
