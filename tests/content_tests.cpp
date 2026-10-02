#include "doctest.h"

#include "core/startup_flow.h"
#include "game/club_loader.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"
#include "game/json_util.h"
#include "game/reward_rules.h"
#include "game/scorecard.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "game/tuning_loader.h"
#include "physics/ground_mesh.h"
#include "renderer/bmp_image.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/overlay_batch.h"
#include "renderer/scorecard_overlay.h"

#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>
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
    CHECK(course->backdrop == "backdrops/fixture.bmp");
    CHECK(course_hole_path("root", *course, 0) == (std::filesystem::path("root") / "holes" / "test.json").string());
    CHECK(course_hole_path("root", *course, 3).empty());

    CHECK(!parse_course_from_text(R"({"id": "empty", "backdrop": "b.bmp", "holes": []})"));
    CHECK(!parse_course_from_text(R"({"id": "bad", "backdrop": "b.bmp", "holes": ["ok", 3]})"));
    CHECK(!parse_course_from_text(R"({"id": "no_backdrop", "holes": ["ok"]})"));
}

TEST_CASE("bmp images decode bottom row first") {
    // 4 x 2: red, green, blue, white along the bottom; black, grey, yellow,
    // cyan along the top.
    const std::optional<rgb_image> image = load_bmp_file(fixture_root() + "/backdrops/fixture.bmp");
    REQUIRE(image.has_value());
    CHECK(image->width == 4);
    CHECK(image->height == 2);
    REQUIRE(image->pixels.size() == 24U);
    const auto pixel = [&image](const int row, const int column) {
        const std::size_t at = static_cast<std::size_t>((row * image->width + column) * 3);
        return std::vector<int>{image->pixels[at], image->pixels[at + 1], image->pixels[at + 2]};
    };
    CHECK((pixel(0, 0) == std::vector<int>{255, 0, 0}));
    CHECK((pixel(0, 2) == std::vector<int>{0, 0, 255}));
    CHECK((pixel(1, 2) == std::vector<int>{255, 255, 0}));
    CHECK((pixel(1, 3) == std::vector<int>{0, 255, 255}));

    const std::string bytes = *read_text_file(fixture_root() + "/backdrops/fixture.bmp");
    CHECK(!parse_bmp(bytes.substr(0, bytes.size() - 4)));
    CHECK(!parse_bmp("BM not an image at all, just some text long enough to pass the header size"));
    CHECK(!load_bmp_file(fixture_root() + "/backdrops/missing.bmp"));
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

TEST_CASE("a hole's bank is optional, one number per control point") {
    const std::string start = R"({"tee": [0, 0, 0], "pin": [0, 0, 100], "spline": {"width": 20, "control_points": [[0, 0, 0], [0, 0, 100]])";
    const std::optional<hole_data> level = parse_hole_from_text(start + "}}");
    REQUIRE(level.has_value());
    CHECK(level->spline.bank.empty());
    const std::optional<hole_data> banked = parse_hole_from_text(start + R"(, "bank": [0.05, -0.02]}})");
    REQUIRE(banked.has_value());
    REQUIRE(banked->spline.bank.size() == 2U);
    CHECK(near(banked->spline.bank[1], -0.02f));
    CHECK(!parse_hole_from_text(start + R"(, "bank": [0.05]}})"));
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

TEST_CASE("every shipped course, hole, world and image loads") {
    const game_content& content = shipped_content();
    // A course file the loader refuses would drop out of the menus silently.
    CHECK(content.courses.size() == json_files_in_directory(std::filesystem::path(content.asset_root) / "courses").size());
    const std::optional<rgb_image> grass = load_bmp_file(std::filesystem::path(content.asset_root) / "textures" / "rough_grass.bmp");
    CHECK(grass.has_value());
    for (const course_definition& course : content.courses) {
        const std::optional<rgb_image> backdrop = load_bmp_file(std::filesystem::path(content.asset_root) / course.backdrop);
        CHECK(backdrop.has_value());
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

namespace {
// The low-res target sizes (see renderer::ensure_framebuffer_size) for 4:3,
// 16:9 and 21:9 windows. The target keeps about the same pixel count at any
// window size, so these cover every resolution.
const std::vector<overlay_grid> shipped_grids{{554, 416}, {640, 360}, {733, 314}};

const text_assets& shipped_text_assets() {
    static const text_assets text = *load_text_assets(asset_root());
    return text;
}

// A scorecard for `course` with every hole played over par, so every column
// has its widest text.
scorecard_data played_scorecard(const course_definition& course, const text_assets& text) {
    scorecard_data scorecard;
    scorecard.course_name = course.name;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        const std::optional<hole_data> hole = load_hole_from_file(asset_root() + "/" + course.holes[i]);
        CHECK(hole.has_value());
        if (!hole) {
            continue;
        }
        scorecard_row row;
        row.hole_number = static_cast<int>(i) + 1;
        row.hole_name = hole->name;
        row.par = hole->par;
        row.played = true;
        row.strokes = hole->par + 10;
        row.relative_label = format_relative_score(text.strings, 10);
        scorecard.rows.push_back(row);
        scorecard.total_par += row.par;
        scorecard.total_strokes += row.strokes;
    }
    scorecard.total_relative_label = format_relative_score(text.strings, 10 * static_cast<int>(course.holes.size()));
    scorecard.finished = true;
    return scorecard;
}

// Every in-round HUD element at once, with the widest values the game shows.
render_data busy_hud(const text_assets& text, const std::string& club_label) {
    render_data data;
    data.show_power_meter = true;
    data.swing_power = 1.0f;
    data.cart_active = true;
    data.cart_drifting = true;
    data.selected_club_label = club_label;
    data.show_skills_panel = true;
    for (const skill_definition& skill : shipped_content().skills) {
        data.skills.push_back(render_skill_progress{lookup_text(text, skill_text_key(skill.id).c_str()), 99, 9999999, 9999999});
    }
    data.xp_drops.push_back(render_xp_drop{skill_icon_id::generic, 99999, 0.5f});
    data.show_rangefinder = true;
    data.rangefinder_label = format_text(text, text_hud_rangefinder, {{"meters", "999"}});
    return data;
}
}

TEST_CASE("every shipped screen fits its text without cutting any off") {
    const text_assets& text = shipped_text_assets();
    const startup_catalog catalog = load_startup_catalog(shipped_content());
    for (const overlay_grid& grid : shipped_grids) {
        for (const startup_flow flow : {startup_flow::main, startup_flow::help, startup_flow::hole_picker, startup_flow::course_picker}) {
            startup_flow_state state;
            state.flow = flow;
            overlay_batch batch;
            batch.grid = grid;
            draw_startup_menu(batch, text, make_startup_menu_render_data(state, catalog, text));
            CHECK(batch.truncated_text_count == 0U);
        }

        startup_flow_state confirm;
        open_confirm_menu(confirm);
        overlay_batch confirm_batch;
        confirm_batch.grid = grid;
        draw_startup_menu(confirm_batch, text, make_confirm_menu_render_data(confirm, text));
        CHECK(confirm_batch.truncated_text_count == 0U);

        for (const club_definition& club : shipped_content().clubs) {
            overlay_batch batch;
            batch.grid = grid;
            draw_hud(batch, text, busy_hud(text, club.label), glm::mat4(1.0f));
            CHECK(batch.truncated_text_count == 0U);
        }

        for (const course_definition& course : shipped_content().courses) {
            const scorecard_data scorecard = played_scorecard(course, text);
            overlay_batch compact;
            compact.grid = grid;
            draw_compact_scorecard(compact, text, scorecard);
            CHECK(compact.truncated_text_count == 0U);

            overlay_batch results;
            results.grid = grid;
            draw_course_results(results, text, scorecard);
            CHECK(results.truncated_text_count == 0U);
        }
    }
}
