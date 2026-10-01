#include "doctest.h"

#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"
#include "game/json_util.h"
#include "game/reward_rules.h"
#include "game/tuning_loader.h"
#include "physics/ground_mesh.h"

#include "test_support.h"

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("the shipped content loads") {
    const game_content_load_result result = load_game_content(asset_root());
    REQUIRE(result.content.has_value());
    CHECK(result.error.empty());
    CHECK(!result.content->courses.empty());
    CHECK(!result.content->skills.empty());
}

TEST_CASE("missing content is reported, not filled in") {
    const game_content_load_result result = load_game_content(fixture_root());
    CHECK(!result.content.has_value());
    CHECK(result.error.find("tuning/game_tuning.json") != std::string::npos);
}

TEST_CASE("game tuning names every missing field") {
    const game_tuning_parse_result empty = parse_game_tuning_from_text("{}");
    CHECK(!empty.tuning.has_value());
    CHECK(empty.error.find("ball.stop_speed") != std::string::npos);
    CHECK(empty.error.find("camera.fov_degrees") != std::string::npos);
    CHECK(!parse_game_tuning_from_text("not json").tuning.has_value());
}

TEST_CASE("the shipped bag is in order with labels and a rising power ladder") {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    const std::vector<std::string> ids{"putter", "sand_wedge", "pitching_wedge", "nine_iron", "seven_iron",
                                       "five_iron", "seven_wood", "four_wood", "driver"};
    const std::vector<std::string> labels{"P", "SWDG", "PWDG", "9I", "7I", "5I", "7WD", "4WD", "DRVR"};
    REQUIRE(clubs.size() == ids.size());
    for (std::size_t i = 0; i < clubs.size(); ++i) {
        CHECK(clubs[i].id == ids[i]);
        CHECK(clubs[i].label == labels[i]);
        CHECK(!clubs[i].hit_sound.empty());
    }
    for (std::size_t i = 2; i + 1 < clubs.size(); ++i) {
        CHECK(clubs[i + 1].stats.power > clubs[i].stats.power);
    }
}

TEST_CASE("clubs need stats and a hit sound") {
    const std::optional<club_definition> club = parse_club_from_text(R"({
      "id": "test_iron", "hit_sound": "club_hit_iron",
      "stats": { "power": 30, "loft_degrees": 30, "backspin": 0.1, "side_spin": 0.5 }
    })");
    REQUIRE(club.has_value());
    CHECK(club->name == "test_iron");
    CHECK(club->stats.timing_speed == 1.0f);
    CHECK(club->stats.roll_friction_scale == 1.0f);
    CHECK(!parse_club_from_text(R"({"id": "x", "stats": {"power": 1, "loft_degrees": 1, "backspin": 0, "side_spin": 0}})"));
}

TEST_CASE("every reward pays into a listed skill") {
    const game_content& content = shipped_content();
    const auto listed = [&content](const std::string& id) {
        return std::any_of(content.skills.begin(), content.skills.end(), [&id](const skill_definition& skill) { return skill.id == id; });
    };
    const reward_rules& rewards = content.rewards;
    for (const std::string& skill : {rewards.shot.skill_id, rewards.smoke.skill_id, rewards.walking.skill_id,
                                     rewards.cart_on_road.skill_id, rewards.drift_on_road.skill_id}) {
        CHECK(listed(skill));
    }
    CHECK(!parse_rewards_from_text("{}").has_value());
}

TEST_CASE("holes parse with tree defaults and reject bad geometry") {
    const std::optional<hole_data> hole = parse_hole_from_text(R"({
      "id": "tree_hole", "par": 4,
      "tee": [0, 0, 0], "pin": [0, 0, 40],
      "spline": { "control_points": [[0, 0, 0], [0, 0, 40]], "width": 12, "rough_width": 24 },
      "trees": [ { "position": [14, 0, 20], "leaf_radius": 2.5 }, { "position": ["bad", 0, 20] } ],
      "material_zones": [ { "type": "lava", "center": [0, 0, 20], "radius": 3 } ]
    })");
    REQUIRE(hole.has_value());
    CHECK(hole->name == "tree_hole");
    CHECK(hole->par == 4);
    CHECK(hole->wind_seed == default_wind_seed);
    CHECK(near(hole->spline.rough_width, 24.0f));
    REQUIRE(hole->trees.size() == 1U);
    CHECK(near(hole->trees[0].shape.trunk_radius, default_tree_shape.trunk_radius));
    CHECK(near(hole->trees[0].shape.leaf_radius, 2.5f));
    REQUIRE(hole->material_zones.size() == 1U);
    CHECK(hole->material_zones[0].type == material_zone_type::unknown);

    const std::optional<hole_data> no_rough = parse_hole_from_text(R"({
      "tee": [0, 0, 0], "pin": [0, 0, 40], "spline": { "control_points": [[0, 0, 0], [0, 0, 40]], "width": 12 }
    })");
    REQUIRE(no_rough.has_value());
    CHECK(near(no_rough->spline.rough_width, 12.0f));

    CHECK(!parse_hole_from_text(R"({"tee": [0, 0, 0], "pin": [0, 0, 40], "spline": {"control_points": [[0, 0, 0]], "width": 12}})"));
    CHECK(!parse_hole_from_text(R"({"tee": [0, 0, 0], "pin": [0, 0, 40], "spline": {"control_points": [[0, 0, 0], [0, 0, 9]], "width": 0}})"));
}

TEST_CASE("courses list holes by id or path") {
    const std::optional<course_definition> course = load_course_from_file(fixture_root() + "/courses/course_01.json");
    REQUIRE(course.has_value());
    CHECK(course->id == "course_01");
    CHECK(course->name == "The Big Three");
    CHECK((course->holes == std::vector<std::string>{"test", "test2", "test3"}));
    CHECK(course_hole_path("root", *course, 0) == (std::filesystem::path("root") / "holes" / "test.json").string());
    CHECK(course_hole_path("root", *course, 3).empty());

    CHECK(!parse_course_from_text(R"({"id": "empty", "holes": []})"));
    CHECK(!parse_course_from_text(R"({"id": "bad", "holes": ["ok", 3]})"));
}

TEST_CASE("course worlds need exactly one start per hole") {
    const course_definition course = fixture_hub_course();
    const std::optional<course_world_definition> world =
        load_course_world_from_file(course_world_file_path(fixture_root(), course), course);
    REQUIRE(world.has_value());
    REQUIRE(world->hole_starts.size() == 3U);
    for (std::size_t i = 0; i < world->hole_starts.size(); ++i) {
        CHECK(world->hole_starts[i].hole_index == static_cast<int>(i));
    }
    CHECK(world->cart_roads.size() == 1U);
    CHECK(world->collectibles.size() == 3U);
    CHECK(world->collectibles[2].requirement.min_level == 2);

    const course_definition one_hole = fixture_course({"test"});
    CHECK(!parse_course_world_from_text(
        R"({"hole_starts": [{"hole_index": 4, "position": [0, 0, 0], "return_position": [1, 0, 1]}]})", one_hole));
    CHECK(!parse_course_world_from_text(R"({"hole_starts": []})", one_hole));
    const course_definition two_holes = fixture_course({"test", "test2"});
    CHECK(!parse_course_world_from_text(R"({"hole_starts": [
        {"hole_index": 0, "position": [0, 0, 0], "return_position": [1, 0, 1]},
        {"hole_index": 0, "position": [9, 0, 0], "return_position": [1, 0, 1]}]})", two_holes));
}

TEST_CASE("course worlds need a complete ground grid") {
    const course_definition course = fixture_hub_course();
    const std::optional<course_world_definition> world =
        load_course_world_from_file(course_world_file_path(fixture_root(), course), course);
    REQUIRE(world.has_value());
    CHECK(world->ground.columns * world->ground.rows == static_cast<int>(world->ground.heights.size()));

    const course_definition one_hole = fixture_course({"test"});
    const std::string start = R"("hole_starts": [{"hole_index": 0, "position": [0, 0, 0], "return_position": [1, 0, 1]}])";
    const auto with_ground = [&](const std::string& ground) { return "{" + start + ground + "}"; };
    const std::optional<course_world_definition> parsed = parse_course_world_from_text(
        with_ground(R"(, "ground": {"origin": [5, 7], "cell_size": 10, "columns": 2, "rows": 2, "heights": [0, 1, 2, 3]})"), one_hole);
    REQUIRE(parsed.has_value());
    CHECK(near(sample_height_grid(parsed->ground, 10.0f, 12.0f), 1.5f));
    CHECK(!parse_course_world_from_text(with_ground(""), one_hole));
    CHECK(!parse_course_world_from_text(
        with_ground(R"(, "ground": {"origin": [0, 0], "cell_size": 10, "columns": 2, "rows": 2, "heights": [0, 1, 2]})"), one_hole));
}

TEST_CASE("every shipped course, hole and world loads") {
    const game_content& content = shipped_content();
    for (const course_definition& course : content.courses) {
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            CHECK(load_hole_from_file(course_hole_path(content.asset_root, course, i)).has_value());
        }
        if (!course.world.empty()) {
            CHECK(load_course_world_from_file(course_world_file_path(content.asset_root, course), course).has_value());
        }
    }
}

TEST_CASE("json helpers never throw on wrong types") {
    const std::optional<json> value = parse_json(R"({"n": "x", "v": [1, "a", 3], "o": 5})");
    REQUIRE(value.has_value());
    CHECK(!json_float(*value, "n"));
    CHECK(!json_vec3(*value, "v"));
    CHECK(json_object(*value, "o") == nullptr);
    CHECK(json_string_array(*value, "v") == std::vector<std::string>{"a"});
    CHECK(!json_string(json(5), "n"));
    CHECK(json_files_in_directory(fixture_root() + "/missing").empty());
}
