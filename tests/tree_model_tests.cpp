#include "doctest.h"

#include "game/lighting_fields.h"
#include "game/json_util.h"
#include "game/lighting_loader.h"
#include "game/tree_looks.h"
#include "renderer/frustum.h"
#include "renderer/render_tree.h"
#include "renderer/tree_pose_drift.h"
#include "renderer/tree_model.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
const lighting_palette& shipped_palette() {
    static const lighting_palette palette = *load_lighting(asset_root());
    return palette;
}

const tree_look& look_named(const char* name) {
    const std::vector<tree_look>& looks = shipped_palette().trees;
    return *std::find_if(looks.begin(), looks.end(), [&](const tree_look& look) { return look.name == name; });
}

tree_body standing(const glm::vec3& base, const std::uint8_t species = 0) {
    tree_body tree;
    tree.base = base;
    tree.shape = tree_shape{0.4f, 5.0f, 4.0f, 9.0f};
    tree.shape.species = species;
    return tree;
}
}

TEST_CASE("every species the trees are of has a look to be drawn in") {
    for (const tree_species& species : shipped_content().tuning.trees.species) {
        const std::vector<tree_look>& looks = shipped_palette().trees;
        CHECK(std::any_of(looks.begin(), looks.end(), [&](const tree_look& look) { return look.name == species.name; }));
    }
}

TEST_CASE("a grown tree is the same for its seed, its bark first, and stands in its unit frame") {
    for (const tree_look& look : shipped_palette().trees) {
        const grown_tree tree = grow_tree(look, 7U);
        const grown_tree again = grow_tree(look, 7U);
        const grown_tree other = grow_tree(look, 8U);
        REQUIRE(!tree.indices.empty());
        REQUIRE(tree.indices.size() % 3U == 0U);
        CHECK(tree.indices == again.indices);
        CHECK(tree.vertices.size() == again.vertices.size());
        CHECK(tree.vertices[tree.vertices.size() / 2].position == again.vertices[again.vertices.size() / 2].position);
        CHECK(tree.vertices.back().position != other.vertices.back().position);
        bool cards = false;
        std::size_t bark = 0;
        for (std::size_t i = 0; i < tree.indices.size(); ++i) {
            REQUIRE(tree.indices[i] < tree.vertices.size());
            const float leaf = tree.vertices[tree.indices[i]].leaf;
            CHECK((leaf == 0.0f || leaf == 1.0f));
            cards = cards || leaf == 1.0f;
            CHECK((!cards || leaf == 1.0f));
            bark += leaf == 0.0f ? 1U : 0U;
        }
        CHECK(bark > 0U);
        CHECK(bark < tree.indices.size());
        CHECK(tree.vertices.size() < tree.indices.size());
        std::set<std::string> distinct;
        for (const grown_tree_vertex& v : tree.vertices) {
            distinct.emplace(reinterpret_cast<const char*>(&v), sizeof v);
        }
        CHECK(distinct.size() == tree.vertices.size());
        for (const grown_tree_vertex& v : tree.vertices) {
            CHECK(v.position.y >= -0.05f);
            CHECK(v.position.y <= 1.25f);
            CHECK(std::abs(v.position.x) <= 1.4f);
            CHECK(std::abs(v.position.z) <= 1.4f);
            CHECK(near(glm::length(v.normal), 1.0f, 0.01f));
        }
    }
}

namespace {
std::size_t bark_triangles(const grown_tree& tree) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < tree.indices.size(); i += 3) {
        count += tree.vertices[tree.indices[i]].leaf == 0.0f ? 1U : 0U;
    }
    return count;
}
}

TEST_CASE("a grown skeleton's stems run coarse orders first, thinner by the pipe model, inside the crown") {
    for (const tree_look& look : shipped_palette().trees) {
        const tree_skeleton skeleton = grow_skeleton(look, 11U);
        REQUIRE(!skeleton.stems.empty());
        for (std::size_t i = 1; i < skeleton.stems.size(); ++i) {
            CHECK(skeleton.stems[i - 1].order <= skeleton.stems[i].order);
        }
        std::vector<float> children_sum(skeleton.nodes.size(), 0.0f);
        std::vector<int> children(skeleton.nodes.size(), 0);
        for (const tree_skeleton::node& node : skeleton.nodes) {
            if (node.parent >= 0) {
                children_sum[static_cast<std::size_t>(node.parent)] += std::pow(node.radius, look.pipe_exponent);
                ++children[static_cast<std::size_t>(node.parent)];
            }
        }
        float thickest = 0.0f;
        for (std::size_t n = 0; n < skeleton.nodes.size(); ++n) {
            const tree_skeleton::node& node = skeleton.nodes[n];
            CHECK(node.radius > 0.0f);
            if (node.parent < 0) {
                thickest = std::max(thickest, node.radius);
            } else {
                // No child is thicker than its parent; a parent is exactly its children's pipes.
                CHECK(node.radius <= skeleton.nodes[static_cast<std::size_t>(node.parent)].radius * 1.0001f);
            }
            if (children[n] > 0) {
                CHECK(near(std::pow(node.radius, look.pipe_exponent), children_sum[n], 1.0e-3f * children_sum[n]));
            }
        }
        CHECK(near(thickest, look.trunk_radius, 1.0e-4f));
        // Every branch (not the trunk below the crown) stays inside the crown's shape.
        for (const tree_skeleton::stem& stem : skeleton.stems) {
            for (std::size_t k = 1; k < stem.nodes.size(); ++k) {
                const tree_skeleton::node& node = skeleton.nodes[static_cast<std::size_t>(stem.nodes[k])];
                if (stem.order > 0) {
                    CHECK(crown_reach(look, node.position) <= 1.0f);
                }
            }
        }
        // The clumps grow from the branch ends: every end has one.
        std::size_t ends = 0;
        for (std::size_t n = 0; n < skeleton.nodes.size(); ++n) {
            if (children[n] != 0 || skeleton.nodes[n].parent < 0) {
                continue;
            }
            ++ends;
            const glm::vec3 end = skeleton.nodes[n].position;
            CHECK(std::any_of(skeleton.clumps.begin(), skeleton.clumps.end(),
                              [&](const tree_skeleton::clump& clump) { return clump.base == end; }));
        }
        CHECK(ends > 0U);
        CHECK(skeleton.clumps.size() == static_cast<std::size_t>(look.clumps));
    }
}

TEST_CASE("a grown tree's bark costs at most a fifth more than the old limbs and spokes did") {
    for (const tree_look& look : shipped_palette().trees) {
        for (std::uint32_t seed = 1U; seed < 40U; ++seed) {
            const grown_tree tree = grow_tree(look, seed);
            const std::size_t bark = bark_triangles(tree);
            // A six-sided trunk and six limbs and a three-sided spoke a clump: about 670 for a broadleaf.
            const std::size_t old_bark = 12U + 6U * 12U + static_cast<std::size_t>(look.clumps) * 6U;
            CHECK(bark <= old_bark * 6U / 5U);
            CHECK(tree.indices.size() / 3U - bark == static_cast<std::size_t>(look.clumps) * 6U);
        }
    }
}

TEST_CASE("a crown's shape is 0 at its middle and 1 at its surface; a spruce narrows to its top") {
    const tree_look& beech = look_named("beech");
    const float middle_y = beech.trunk_share + 0.5f * (1.0f - beech.trunk_share);
    CHECK(near(crown_reach(beech, glm::vec3(0.0f, middle_y, 0.0f)), 0.0f));
    CHECK(near(crown_reach(beech, glm::vec3(1.0f, middle_y, 0.0f)), 1.0f));
    CHECK(crown_reach(beech, glm::vec3(0.0f, 0.05f, 0.0f)) > 1.0f);
    const tree_look& spruce = look_named("spruce");
    CHECK(crown_reach(spruce, glm::vec3(0.5f, 0.3f, 0.0f)) < 1.0f);
    CHECK(crown_reach(spruce, glm::vec3(0.5f, 0.9f, 0.0f)) > 1.0f);
    // A bush is all crown from the ground.
    CHECK(crown_reach(look_named("scrub"), glm::vec3(0.5f, 0.2f, 0.0f)) < 1.0f);
}

TEST_CASE("a tree stands at its foot, as tall as it is and as wide as its crown, turned about its trunk") {
    const tree_body tree = standing(glm::vec3(10.0f, 2.0f, -4.0f));
    const render_tree_instance instance = tree_instance_for(tree);
    CHECK(near(tree_world_point(instance, glm::vec3(0.0f)), tree.base));
    CHECK(near(tree_world_point(instance, glm::vec3(0.0f, 1.0f, 0.0f)), tree.base + glm::vec3(0.0f, 14.0f, 0.0f)));
    const glm::vec3 edge = tree_world_point(instance, glm::vec3(1.0f, 0.5f, 0.0f)) - tree.base;
    CHECK(near(glm::length(glm::vec2(edge.x, edge.z)), 4.0f));
    CHECK(near(edge.y, 7.0f));
    CHECK(instance.tint >= 0.0f);
    CHECK(instance.tint <= 1.0f);
    CHECK(tree_variant_for(tree) >= 0);
    CHECK(tree_variant_for(tree) < tree_variants);
}

namespace {
struct pose {
    glm::vec3 eye;
    glm::vec3 target;
    float fov_degrees;
};

view_frustum frustum_of(const pose& view) {
    return make_view_frustum(glm::perspective(glm::radians(view.fov_degrees), 16.0f / 9.0f, 0.1f, 600.0f) *
                             glm::lookAt(view.eye, view.target, glm::vec3(0.0f, 1.0f, 0.0f)));
}

// The brute-force oracle: every tree one by one against its own bounds.
bool sees(const view_frustum& frustum, const tree_body& tree) {
    const tree_bounds bounds = tree_bounds_for(tree_instance_for(tree));
    return frustum_intersects_aabb(frustum, bounds.min, bounds.max);
}

std::vector<tree_body> scattered_trees() {
    std::vector<tree_body> trees;
    std::uint32_t seed = 12345U;
    const auto next = [&seed]() {
        seed = seed * 1664525U + 1013904223U;
        return static_cast<float>(seed >> 8) / 16777216.0f;
    };
    for (int i = 0; i < 1500; ++i) {
        tree_body tree = standing(glm::vec3(next() * 900.0f - 450.0f, next() * 20.0f - 10.0f, next() * 900.0f - 450.0f),
                                  static_cast<std::uint8_t>(i % 4));
        // Some crowns are huge, so a tree is seen from a cell away.
        if (i % 25 == 0) {
            tree.shape = tree_shape{1.0f, 20.0f, 30.0f, 40.0f};
            tree.shape.species = static_cast<std::uint8_t>(i % 4);
        }
        trees.push_back(tree);
    }
    return trees;
}
}

TEST_CASE("a tree's bounds hold every leaf at the strongest wind, at any gust and yaw") {
    CHECK(near(60.0f, max_wind_speed));
    for (const tree_look& look : shipped_palette().trees) {
        const grown_tree model = grow_tree(look, 3U);
        for (const glm::vec3 base : {glm::vec3(10.0f, 2.0f, -4.0f), glm::vec3(-250.5f, -3.0f, -640.25f)}) {
            for (const tree_shape& shape : {tree_shape{0.4f, 5.0f, 4.0f, 9.0f}, tree_shape{1.0f, 20.0f, 30.0f, 40.0f}}) {
                tree_body tree;
                tree.base = base;
                tree.shape = shape;
                const render_tree_instance instance = tree_instance_for(tree);
                const tree_bounds bounds = tree_bounds_for(instance);
                for (const grown_tree_vertex& v : model.vertices) {
                    for (const glm::vec2 wind : {glm::vec2(60.0f, 0.0f), glm::vec2(-60.0f, 0.0f), glm::vec2(0.0f, 60.0f),
                                                 glm::vec2(0.0f, -60.0f), glm::vec2(0.0f)}) {
                        // tree_pose.glsl at its largest gust.
                        glm::vec3 world = tree_world_point(instance, v.position);
                        world.x += wind.x * 0.004f * instance.scale.y * v.position.y * v.position.y;
                        world.z += wind.y * 0.004f * instance.scale.y * v.position.y * v.position.y;
                        REQUIRE(world.x >= bounds.min.x);
                        REQUIRE(world.x <= bounds.max.x);
                        REQUIRE(world.y >= bounds.min.y);
                        REQUIRE(world.y <= bounds.max.y);
                        REQUIRE(world.z >= bounds.min.z);
                        REQUIRE(world.z <= bounds.max.z);
                    }
                }
            }
        }
    }
}

TEST_CASE("the index selects every tree the brute-force test sees, in runs of the instances it built") {
    const std::vector<tree_body> trees = scattered_trees();
    tree_index index;
    index.build(trees);
    REQUIRE(index.instances().size() == trees.size());

    std::vector<render_tree_bucket> all;
    std::vector<render_tree_bucket> far_runs;
    index.select(nullptr, nullptr, all, far_runs);
    std::size_t total = 0;
    for (const render_tree_bucket& bucket : all) {
        total += bucket.count;
    }
    CHECK(total == trees.size());

    const std::vector<pose> poses{
        {{0.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 100.0f}, 60.0f},
        {{-300.0f, 1.7f, -400.0f}, {-200.0f, 1.0f, -250.0f}, 40.0f},
        {{200.0f, 30.0f, 300.0f}, {180.0f, 0.0f, 280.0f}, 90.0f},
        // Looking up from beneath: a mirrored view across a pond.
        {{-10.0f, -5.0f, 40.0f}, {30.0f, 12.0f, 90.0f}, 50.0f},
        {{449.0f, 5.0f, 449.0f}, {-449.0f, 5.0f, -449.0f}, 20.0f},
    };
    std::vector<render_tree_bucket> ranges;
    for (const pose& view : poses) {
        const view_frustum frustum = frustum_of(view);
        index.select(&frustum, nullptr, ranges, far_runs);
        std::size_t last_end = 0;
        std::size_t submitted = 0;
        for (const render_tree_bucket& range : ranges) {
            CHECK(range.count > 0U);
            last_end = range.first + range.count;
            REQUIRE(last_end <= trees.size());
            submitted += range.count;
        }
        std::size_t seen = 0;
        for (const tree_body& tree : trees) {
            if (!sees(frustum, tree)) {
                continue;
            }
            ++seen;
            // Its instance stands in a run of its own species and variant.
            const render_tree_instance wanted = tree_instance_for(tree);
            bool found = false;
            for (const render_tree_bucket& range : ranges) {
                for (std::size_t k = range.first; k < range.first + range.count; ++k) {
                    const render_tree_instance& have = index.instances()[k];
                    if (have.offset == wanted.offset && have.scale == wanted.scale && have.yaw == wanted.yaw &&
                        have.tint == wanted.tint && range.species == tree.shape.species &&
                        range.variant == tree_variant_for(tree)) {
                        found = true;
                    }
                }
            }
            CHECK(found);
        }
        CHECK(submitted >= seen);
        // A view takes in a modest share beyond what it sees, not the course.
        CHECK(submitted < trees.size());
    }
}

TEST_CASE("the index selects nothing for no trees or a view of nothing, and a new build replaces the old") {
    tree_index index;
    std::vector<render_tree_bucket> ranges{render_tree_bucket{}};
    std::vector<render_tree_bucket> far_runs;
    index.build({});
    CHECK(index.empty());
    index.select(nullptr, nullptr, ranges, far_runs);
    CHECK(ranges.empty());
    const std::vector<tree_body> trees{standing(glm::vec3(0.0f, 0.0f, 20.0f), 0), standing(glm::vec3(3.0f, 0.0f, 300.0f), 0),
                                       standing(glm::vec3(-2.0f, 0.0f, 25.0f), 1), standing(glm::vec3(0.0f, 0.0f, -50.0f), 0)};
    index.build(trees);
    CHECK(index.instances().size() == 4U);
    const view_frustum ahead = frustum_of({{0.0f, 2.0f, 0.0f}, {0.0f, 2.0f, 100.0f}, 60.0f});
    index.select(&ahead, nullptr, ranges, far_runs);
    std::size_t counted = 0;
    for (const render_tree_bucket& range : ranges) {
        counted += range.count;
    }
    CHECK(counted == 3U);  // not the one behind
    const view_frustum sky = frustum_of({{0.0f, 2.0f, 0.0f}, {0.0f, 500.0f, 1.0f}, 20.0f});
    index.select(&sky, nullptr, ranges, far_runs);
    CHECK(ranges.empty());
    index.build({trees.front()});
    CHECK(index.instances().size() == 1U);
}

TEST_CASE("the shipped image grid is read, and its baked images fit the cache budget") {
    const tree_impostor_style& style = shipped_palette().tree_impostor;
    REQUIRE(style.azimuths >= 1);
    REQUIRE(!style.elevations_degrees.empty());
    CHECK(std::is_sorted(style.elevations_degrees.begin(), style.elevations_degrees.end()));
    CHECK(style.max_error_pixels > 0.0f);
    // Colour, normal with depth, and leaf share and depth: ten bytes a texel, a third more for the mip chain,
    // for every look's every variant (docs/todo/rendering_performance.md: at most 24 MiB).
    const double views = static_cast<double>(style.azimuths) * static_cast<double>(style.elevations_degrees.size());
    const double bytes = static_cast<double>(shipped_palette().trees.size()) * tree_variants * views *
                         style.tile_pixels * style.tile_pixels * 10.0 * (4.0 / 3.0);
    CHECK(bytes <= 24.0 * 1024.0 * 1024.0);
}

TEST_CASE("an image grid with no views, unordered or too many elevations, or a missing number is refused") {
    const auto parsed = [](const std::string& grid) {
        return parse_tree_impostor_style(json::parse("{\"trees\":{\"impostor\":" + grid + "}}"));
    };
    const std::string tail = ", \"tile_pixels\": 32, \"radius_error\": 0.11, \"wind_error\": 0.0005, \"max_error_pixels\": 0.5, \"depth_first_coverage\": 1.5, \"depth_first_coverage_lamps\": 0.05}";
    const auto grid = [&](const std::string& azimuths, const std::string& elevations) {
        return "{\"azimuths\": " + azimuths + ", \"elevations_degrees\": " + elevations + tail;
    };
    CHECK(parsed(grid("12", "[-15, 5, 40]")).has_value());
    CHECK(!parsed(grid("0", "[5]")).has_value());
    CHECK(!parsed(grid("17", "[5]")).has_value());
    CHECK(!parsed(grid("12", "[]")).has_value());
    CHECK(!parsed(grid("12", "[5, 5]")).has_value());
    CHECK(!parsed(grid("12", "[10, -10]")).has_value());
    CHECK(!parsed(grid("12", "[-1, 0, 1, 2, 3, 4, 5, 6, 7]")).has_value());
    CHECK(!parsed(grid("12", "[85]")).has_value());
    CHECK(!parsed("{\"azimuths\": 12, \"elevations_degrees\": [5], \"tile_pixels\": 32}").has_value());
}

TEST_CASE("an image's error grows with the crown and the wind and shrinks with distance") {
    tree_lod_view view;
    view.focal_pixels = 400.0f;
    view.radius_error = 0.06f;
    view.wind_error = 0.004f;
    const float near_error = tree_image_error_pixels(view, 4.0f, 14.0f, 100.0f);
    CHECK(near(near_error, 0.06f * 4.0f * 400.0f / 100.0f));
    CHECK(near(tree_image_error_pixels(view, 4.0f, 14.0f, 200.0f), near_error * 0.5f));
    CHECK(tree_image_error_pixels(view, 8.0f, 14.0f, 100.0f) > near_error);
    view.wind_speed = 6.0f;
    CHECK(near(tree_image_error_pixels(view, 4.0f, 14.0f, 100.0f), (0.06f * 4.0f + 0.004f * 6.0f * 14.0f) * 4.0f));
    CHECK(std::isinf(tree_image_error_pixels(view, 4.0f, 14.0f, 0.0f)));
    view.focal_pixels = 0.0f;
    CHECK(std::isinf(tree_image_error_pixels(view, 4.0f, 14.0f, 100.0f)));
    view.focal_pixels = 400.0f;
    // A larger focal length (a zoomed or finer picture) makes the same tree's image worse.
    view.focal_pixels = 800.0f;
    CHECK(tree_image_error_pixels(view, 4.0f, 14.0f, 100.0f) > near_error);
}

TEST_CASE("far runs hold only trees whose images are within the error, and never a tree the near runs hold") {
    const std::vector<tree_body> trees = scattered_trees();
    tree_index index;
    index.build(trees);
    const pose looking{{0.0f, 2.0f, -400.0f}, {0.0f, 6.0f, 100.0f}, 70.0f};
    const view_frustum frustum = frustum_of(looking);
    tree_lod_view lod;
    lod.eye = looking.eye;
    lod.focal_pixels = 380.0f;
    lod.max_error_pixels = 0.5f;
    lod.radius_error = 0.06f;
    lod.wind_error = 0.004f;
    std::vector<render_tree_bucket> near_runs;
    std::vector<render_tree_bucket> far_runs;
    for (const float wind : {0.0f, 8.0f}) {
        lod.wind_speed = wind;
        index.select(&frustum, &lod, near_runs, far_runs);
        std::vector<int> listed(trees.size(), 0);
        std::size_t far_count = 0;
        for (const render_tree_bucket& run : near_runs) {
            for (std::size_t k = run.first; k < run.first + run.count; ++k) { listed[k] |= 1; }
        }
        for (const render_tree_bucket& run : far_runs) {
            far_count += run.count;
            for (std::size_t k = run.first; k < run.first + run.count; ++k) {
                listed[k] |= 2;
                const render_tree_instance& instance = index.instances()[k];
                const float distance = glm::length(instance.offset - lod.eye);
                CHECK(tree_image_error_pixels(lod, std::max(instance.scale.x, instance.scale.z), instance.scale.y, distance) <= 0.5f);
            }
        }
        CHECK(far_count > 0U);
        for (const int both : listed) { CHECK(both != 3); }
        // Every tree the brute-force test sees is in a run of one list, and none close enough to matter is an image.
        for (const tree_body& tree : trees) {
            const render_tree_instance wanted = tree_instance_for(tree);
            if (!sees(frustum, tree)) { continue; }
            bool found = false;
            for (std::size_t k = 0; k < trees.size() && !found; ++k) {
                const render_tree_instance& have = index.instances()[k];
                found = have.offset == wanted.offset && listed[k] != 0;
            }
            CHECK(found);
        }
        for (std::size_t k = 0; k < trees.size(); ++k) {
            const render_tree_instance& instance = index.instances()[k];
            if (glm::length(instance.offset - lod.eye) < 20.0f) { CHECK((listed[k] & 2) == 0); }
        }
    }
    // More wind, fewer images; no images without a limit or a lens.
    lod.wind_speed = 0.0f;
    index.select(&frustum, &lod, near_runs, far_runs);
    std::size_t calm = 0;
    for (const render_tree_bucket& run : far_runs) { calm += run.count; }
    lod.wind_speed = 30.0f;
    index.select(&frustum, &lod, near_runs, far_runs);
    std::size_t windy = 0;
    for (const render_tree_bucket& run : far_runs) { windy += run.count; }
    CHECK(windy < calm);
    lod.wind_speed = 0.0f;
    lod.max_error_pixels = 0.0f;
    index.select(&frustum, &lod, near_runs, far_runs);
    CHECK(far_runs.empty());
    lod.max_error_pixels = 0.5f;
    lod.focal_pixels = 0.0f;
    index.select(&frustum, &lod, near_runs, far_runs);
    CHECK(far_runs.empty());
    index.select(&frustum, nullptr, near_runs, far_runs);
    CHECK(far_runs.empty());
    CHECK(!near_runs.empty());
}

TEST_CASE("a model's box holds every vertex of it and no more") {
    for (const tree_look& look : shipped_palette().trees) {
        const grown_tree model = grow_tree(look, 5U);
        const tree_box box = tree_box_of(model);
        glm::vec3 low = box.centre - box.half;
        glm::vec3 high = box.centre + box.half;
        bool touches_low = false;
        bool touches_high = false;
        for (const grown_tree_vertex& v : model.vertices) {
            CHECK(v.position.x >= low.x - 1.0e-5f);
            CHECK(v.position.y >= low.y - 1.0e-5f);
            CHECK(v.position.z >= low.z - 1.0e-5f);
            CHECK(v.position.x <= high.x + 1.0e-5f);
            CHECK(v.position.y <= high.y + 1.0e-5f);
            CHECK(v.position.z <= high.z + 1.0e-5f);
            touches_low = touches_low || std::abs(v.position.y - low.y) < 1.0e-5f;
            touches_high = touches_high || std::abs(v.position.y - high.y) < 1.0e-5f;
        }
        CHECK(touches_low);
        CHECK(touches_high);
    }
    CHECK(tree_box_of(grown_tree{}).half == glm::vec3(0.0f));
}

namespace {
// tree_pose.glsl's sway of a leaf at unit height `local_y` (its crown's top is 1.3), as CPU maths.
glm::vec2 sway_of(const render_tree_instance& tree, const float local_y, const float seconds, const glm::vec2& wind) {
    const float gust = 0.6f + 0.4f * std::sin(seconds * 1.3f + tree.offset.x * 0.21f + tree.offset.z * 0.17f);
    return wind * (0.004f * tree.scale.y * local_y * local_y * gust);
}
}

TEST_CASE("a cached shadow of swaying crowns is good while no leaf can have moved past the tolerance") {
    const render_tree_instance tall = tree_instance_for(standing({20.0f, 0.0f, -7.0f}));
    tree_index index;
    index.build({standing({20.0f, 0.0f, -7.0f}), standing({-30.0f, 0.0f, 12.0f})});
    CHECK(near(index.pose_reach(), tree_pose_reach(tall)));
    const float reach = index.pose_reach();
    const float tolerance = 0.15f;
    tree_pose_drift drift;
    CHECK(!drift.within(0.0f, {0.0f, 0.0f}, reach, tolerance));
    drift.remember(10.0f, {0.0f, 0.0f});
    // A calm day never moves a crown, whatever the clock says.
    CHECK(drift.within(5000.0f, {0.0f, 0.0f}, reach, tolerance));
    CHECK(!drift.within(10.0f, {2.0f, 0.0f}, reach, tolerance));
    // The claim: whenever it says the pose is good, no leaf of the tallest crown has moved more than the tolerance
    // (and in wind it does ask for a new image within seconds).
    for (const glm::vec2 wind : {glm::vec2(3.0f, 0.0f), glm::vec2(0.0f, -8.0f), glm::vec2(6.0f, 6.0f), glm::vec2(30.0f, 0.0f)}) {
        for (float start = 0.0f; start < 6.0f; start += 0.7f) {
            tree_pose_drift held;
            held.remember(start, wind);
            bool refreshed = false;
            for (float later = start; later < start + 40.0f; later += 0.05f) {
                const bool good = held.within(later, wind, reach, tolerance);
                refreshed = refreshed || !good;
                if (!good) { continue; }
                for (const float height : {1.3f, 1.0f, 0.6f}) {
                    const glm::vec2 moved = sway_of(tall, height, later, wind) - sway_of(tall, height, start, wind);
                    CHECK(glm::length(moved) <= tolerance + 1.0e-4f);
                }
            }
            CHECK(refreshed);
        }
    }
    // A changing wind counts too: the same time, wind turned.
    tree_pose_drift turned;
    turned.remember(1.0f, {4.0f, 0.0f});
    CHECK(!turned.within(1.0f, {0.0f, 4.0f}, reach, tolerance));
    CHECK(turned.within(1.0f, {4.0f, 0.0f}, reach, tolerance));
}

TEST_CASE("near crowns' covered picture grows as the eye comes closer and is none for trees drawn as images") {
    std::vector<tree_body> trees;
    for (int i = 0; i < 20; ++i) { trees.push_back(standing({-30.0f + 3.0f * static_cast<float>(i), 0.0f, 40.0f})); }
    tree_index index;
    index.build(trees);
    const pose looking{{0.0f, 2.0f, 0.0f}, {0.0f, 6.0f, 100.0f}, 70.0f};
    const view_frustum frustum = frustum_of(looking);
    tree_lod_view lod;
    lod.eye = looking.eye;
    lod.focal_pixels = 380.0f;
    lod.max_error_pixels = 0.5f;
    lod.radius_error = 0.06f;
    std::vector<render_tree_bucket> near_runs;
    std::vector<render_tree_bucket> far_runs;
    double covered = -1.0;
    index.select(&frustum, &lod, near_runs, far_runs, &covered);
    CHECK(covered > 0.0);
    lod.eye = {0.0f, 2.0f, 20.0f};
    double closer = -1.0;
    index.select(&frustum, &lod, near_runs, far_runs, &closer);
    CHECK(closer > covered);
    // Seen from far away every one of them is an image: nothing left to cover.
    lod.eye = {0.0f, 2.0f, -5000.0f};
    lod.focal_pixels = 1.0f;
    double none = -1.0;
    index.select(nullptr, &lod, near_runs, far_runs, &none);
    CHECK(none == 0.0);
    CHECK(near_runs.empty());
}
