#include "doctest.h"

#include "game/content_files.h"
#include "game/lighting_loader.h"
#include "game/tree_season.h"
#include "renderer/falling_leaf.h"
#include "renderer/render_tree.h"
#include "renderer/tree_model.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {
const lighting_palette& shipped_palette() {
    static const lighting_palette palette = *load_lighting(asset_root());
    return palette;
}

const tree_species& named(const tree_tuning& trees, const char* name) {
    return trees.species[*species_index(trees, name)];
}

const tree_look& look_named(const char* name) {
    const std::vector<tree_look>& looks = shipped_palette().trees;
    return *std::find_if(looks.begin(), looks.end(), [&](const tree_look& look) { return look.name == name; });
}

render_tree_instance beech_at(const glm::vec3& foot) {
    render_tree_instance tree;
    tree.offset = foot;
    tree.scale = glm::vec3(4.0f, 14.0f, 4.0f);
    tree.yaw = 0.7f;
    tree.tint = 0.4f;
    return tree;
}

// How many of a tree's slots show a leaf at `seconds`.
int shown(const falling_leaf_style& style, const tree_box& crown, const render_tree_instance& tree, const float per_cycle,
          const float seconds, const glm::vec2& wind) {
    int count = 0;
    for (int index = 0; index < style.most_per_tree; ++index) {
        count += place_falling_leaf(style, crown, tree, index, per_cycle, seconds, wind).shown ? 1 : 0;
    }
    return count;
}
}

TEST_CASE("leaves fall in autumn, a few in summer, withered ones through a beech's winter, and none from a spruce") {
    const tree_tuning& trees = shipped_content().tuning.trees;
    const tree_species& beech = named(trees, "beech");
    const float autumn = 0.5f * (beech.turned_day + beech.bare_day);
    const float summer = static_cast<float>(day_of_year(7, 1));
    const float winter = static_cast<float>(day_of_year(1, 15));
    float withered = 1.0f;
    const float peak = leaves_falling(trees, beech, autumn, &withered);
    CHECK(near(peak, (1.0f - beech.withered_share) / (beech.bare_day - beech.turned_day), 1.0e-4f));
    CHECK(withered == 0.0f);
    CHECK(leaves_falling(trees, beech, summer) > 0.0f);
    CHECK(leaves_falling(trees, beech, summer) < 0.05f * peak);
    CHECK(leaves_falling(trees, beech, winter, &withered) > 0.0f);
    CHECK(near(withered, 1.0f));
    for (float day = 1.0f; day <= 365.0f; day += 1.0f) {
        CHECK(leaves_falling(trees, named(trees, "spruce"), day) == 0.0f);
        CHECK(leaves_falling(trees, named(trees, "pine"), day) == 0.0f);
    }
    // A birch keeps no withered leaves: its bare winter drops nothing.
    CHECK(leaves_falling(trees, named(trees, "birch"), winter) == 0.0f);
}

TEST_CASE("a falling leaf never goes under the ground at its tree and stays within the slots and the cap") {
    const falling_leaf_style& style = shipped_palette().falling_leaves;
    REQUIRE(style.most_per_tree > 0);
    REQUIRE(style.most_per_pass >= style.most_per_tree);
    const tree_box crown = tree_crown_box(look_named("beech"));
    const render_tree_instance tree = beech_at(glm::vec3(12.0f, 3.5f, -40.0f));
    const float per_cycle = falling_leaves_per_cycle(style, tree, 0.04f, 6.0f);
    CHECK(per_cycle > 0.0f);
    int most = 0;
    int seen = 0;
    for (float seconds = 0.0f; seconds < 120.0f; seconds += 0.37f) {
        for (int index = 0; index < style.most_per_tree; ++index) {
            const falling_leaf leaf = place_falling_leaf(style, crown, tree, index, per_cycle, seconds, glm::vec2(3.0f, -2.0f));
            if (!leaf.shown) {
                continue;
            }
            ++seen;
            for (const float u : {-1.0f, 1.0f}) {
                for (const float v : {-1.0f, 1.0f}) {
                    REQUIRE((leaf.centre + leaf.across * u + leaf.along * v).y >= tree.offset.y - 1.0e-4f);
                }
            }
            CHECK(glm::length(leaf.across) <= 0.5f * style.leaf_size_meters + 1.0e-5f);
            CHECK(leaf.centre.y <= tree.offset.y + 1.3f * tree.scale.y);
        }
        most = std::max(most, shown(style, crown, tree, per_cycle, seconds, glm::vec2(3.0f, -2.0f)));
    }
    CHECK(seen > 0);
    CHECK(most <= style.most_per_tree);
    // Over many trees and cycles, the expected number of slots drop a leaf each cycle.
    double used = 0.0;
    double expected = 0.0;
    for (int t = 0; t < 40; ++t) {
        const render_tree_instance other = beech_at(glm::vec3(static_cast<float>(t) * 9.3f, 0.0f, 5.0f));
        const float per_cycle = falling_leaves_per_cycle(style, other, 0.04f, 0.0f);
        const float cycle_seconds = falling_leaf_cycle_seconds(style, other);
        for (int cycle = 0; cycle < 50; ++cycle) {
            for (int index = 0; index < style.most_per_tree; ++index) {
                used += falling_leaf_slot_used(style, other, index, per_cycle, cycle_seconds * static_cast<float>(cycle)) ? 1.0 : 0.0;
            }
            expected += std::min(per_cycle, static_cast<float>(style.most_per_tree));
        }
    }
    CHECK(std::abs(used / expected - 1.0) < 0.1);
}

TEST_CASE("no leaf falls when none are falling, and the same leaf is in the same place for the same moment") {
    const falling_leaf_style& style = shipped_palette().falling_leaves;
    const tree_box crown = tree_crown_box(look_named("oak"));
    const render_tree_instance tree = beech_at(glm::vec3(-3.0f, 0.0f, 8.0f));
    CHECK(falling_leaves_per_cycle(style, tree, 0.0f, 20.0f) == 0.0f);
    for (float seconds = 0.0f; seconds < 60.0f; seconds += 0.5f) {
        CHECK(shown(style, crown, tree, 0.0f, seconds, glm::vec2(8.0f, 0.0f)) == 0);
    }
    const falling_leaf a = place_falling_leaf(style, crown, tree, 0, 3.0f, 17.25f, glm::vec2(2.0f, 1.0f));
    const falling_leaf b = place_falling_leaf(style, crown, tree, 0, 3.0f, 17.25f, glm::vec2(2.0f, 1.0f));
    CHECK(a.shown == b.shown);
    CHECK(a.centre == b.centre);
    // More fall in wind.
    CHECK(falling_leaves_per_cycle(style, tree, 0.04f, 10.0f) > falling_leaves_per_cycle(style, tree, 0.04f, 0.0f));
}
