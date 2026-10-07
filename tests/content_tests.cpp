#include "doctest.h"

#include "net/online_config.h"

#include "core/startup_flow.h"
#include "game/club_loader.h"
#include "game/content_files.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_loader.h"
#include "game/json_util.h"
#include "game/net_types.h"
#include "game/reward_rules.h"
#include "game/scorecard.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "game/tuning_loader.h"
#include "physics/ground_mesh.h"
#include "game/play_area.h"
#include "renderer/bmp_image.h"
#include "renderer/hole_sign_face.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/overlay_batch.h"
#include "renderer/scorecard_overlay.h"

#include "test_support.h"

#include <array>
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
    CHECK(empty.error.find("camera.walking_eye_height") != std::string::npos);
    CHECK(!parse_game_tuning_from_text("not json").tuning.has_value());
}

TEST_CASE("game tuning needs every lie") {
    const std::optional<std::string> shipped = read_text_file(asset_root() + "/tuning/game_tuning.json");
    REQUIRE(shipped.has_value());
    REQUIRE(parse_game_tuning_from_text(*shipped).tuning.has_value());
    std::string no_bunker = *shipped;
    const std::size_t at = no_bunker.find("\"bunker\": { \"power\"");
    REQUIRE(at != std::string::npos);
    no_bunker.replace(at, 8, "\"sand\"");
    const game_tuning_parse_result result = parse_game_tuning_from_text(no_bunker);
    CHECK(!result.tuning.has_value());
    CHECK(result.error.find("lies.bunker.power") != std::string::npos);
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
      "stats": { "power": 30, "loft_degrees": 30, "backspin": 0.1, "side_spin": 0.5, "bunker_power": 0.4 }
    })");
    REQUIRE(club.has_value());
    CHECK(club->stats.bunker_power == 0.4f);
    CHECK(club->name == "test_iron");
    CHECK(club->stats.timing_speed == 1.0f);
    CHECK(club->stats.roll_friction_scale == 1.0f);
    CHECK(!parse_club_from_text(R"({"id": "x", "stats": {"power": 1, "loft_degrees": 1, "backspin": 0, "side_spin": 0}})"));
    // No bunker_power: how a club plays from sand is not guessed.
    CHECK(!parse_club_from_text(R"({"id": "x", "hit_sound": "club_hit_iron",
      "stats": {"power": 1, "loft_degrees": 1, "backspin": 0, "side_spin": 0}})"));
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
      "material_zones": [ { "type": "lava", "center": [0, 0, 20], "radii": [3, 3], "rotation_degrees": 0 } ]
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
    const std::optional<course_definition> course = load_course_from_file(fixture_root() + "/courses/hub.json");
    REQUIRE(course.has_value());
    CHECK(course->id == "fixture_hub");
    CHECK(course->name == "Fixture Hub");
    CHECK(course->world == "course_worlds/hub.json");
    CHECK((course->holes == std::vector<std::string>{"test", "test2", "test3"}));
    CHECK(course->backdrop.sky == "backdrops/fixture_sky.bmp");
    CHECK(course->backdrop.land == "backdrops/fixture_land.bmp");
    CHECK(near(course->backdrop.haze_color.z, 0.83f));
    CHECK(near(course->backdrop.haze_amount, 1.0f));
    CHECK(near(course->backdrop.haze_distance, 500.0f));
    CHECK(course_hole_path("root", *course, 0) == (std::filesystem::path("root") / "holes" / "test.json").string());
    CHECK(course_hole_path("root", *course, 3).empty());

    const std::string backdrop =
        R"("backdrop": {"sky": "s.bmp", "land": "l.bmp", "haze_color": [1, 1, 1], "haze_amount": 1, "haze_distance": 500})";
    const std::string named = R"("id": "ok", "name": "Ok", "world": "w.json", )";
    CHECK(parse_course_from_text("{" + named + backdrop + R"(, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text("{" + named + backdrop + R"(, "holes": []})"));
    CHECK(!parse_course_from_text("{" + named + backdrop + R"(, "holes": ["ok", 3]})"));
    CHECK(!parse_course_from_text(R"({"name": "Ok", "world": "w.json", )" + backdrop + R"(, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text(R"({"id": "ok", "world": "w.json", )" + backdrop + R"(, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text(R"({"id": "ok", "name": "Ok", )" + backdrop + R"(, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text("{" + named + R"("holes": ["ok"]})"));
    CHECK(!parse_course_from_text("{" + named + R"("backdrop": "b.bmp", "holes": ["ok"]})"));
    CHECK(!parse_course_from_text(
        "{" + named + R"("backdrop": {"sky": "s.bmp", "haze_color": [1, 1, 1], "haze_amount": 1, "haze_distance": 500}, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text(
        "{" + named + R"("backdrop": {"sky": "s.bmp", "land": "l.bmp", "haze_amount": 1, "haze_distance": 500}, "holes": ["ok"]})"));
    CHECK(!parse_course_from_text(
        "{" + named + R"("backdrop": {"sky": "s.bmp", "land": "l.bmp", "haze_color": [1, 1, 1], "haze_amount": 1, "haze_distance": 0}, "holes": ["ok"]})"));
}

namespace {
// The pixel at `row` (from the bottom) and `column` as RGBA.
std::vector<int> bmp_pixel(const rgba_image& image, const int row, const int column) {
    const std::size_t at = static_cast<std::size_t>((row * image.width + column) * 4);
    return {image.pixels[at], image.pixels[at + 1], image.pixels[at + 2], image.pixels[at + 3]};
}
}

TEST_CASE("bmp images decode bottom row first") {
    // 4 x 2, 24-bit: red, green, blue, white along the bottom; black, grey,
    // yellow, cyan along the top. No alpha: every pixel is opaque.
    const std::optional<rgba_image> image = load_bmp_file(fixture_root() + "/backdrops/fixture_sky.bmp");
    REQUIRE(image.has_value());
    CHECK(image->width == 4);
    CHECK(image->height == 2);
    REQUIRE(image->pixels.size() == 32U);
    CHECK((bmp_pixel(*image, 0, 0) == std::vector<int>{255, 0, 0, 255}));
    CHECK((bmp_pixel(*image, 0, 2) == std::vector<int>{0, 0, 255, 255}));
    CHECK((bmp_pixel(*image, 1, 2) == std::vector<int>{255, 255, 0, 255}));
    CHECK((bmp_pixel(*image, 1, 3) == std::vector<int>{0, 255, 255, 255}));

    const std::string bytes = *read_text_file(fixture_root() + "/backdrops/fixture_sky.bmp");
    CHECK(!parse_bmp(bytes.substr(0, bytes.size() - 4)));
    CHECK(!parse_bmp("BM not an image at all, just some text long enough to pass the header size"));
    CHECK(!load_bmp_file(fixture_root() + "/backdrops/missing.bmp"));
}

TEST_CASE("32-bit bmp images with an alpha mask keep their alpha") {
    // 4 x 2, as tooling/art/make_art.py writes the land panoramas: red,
    // green, blue, white along the bottom with alpha 255, 128, 0 and 64.
    const std::optional<rgba_image> image = load_bmp_file(fixture_root() + "/backdrops/fixture_land.bmp");
    REQUIRE(image.has_value());
    CHECK(image->width == 4);
    CHECK(image->height == 2);
    CHECK((bmp_pixel(*image, 0, 0) == std::vector<int>{255, 0, 0, 255}));
    CHECK((bmp_pixel(*image, 0, 1) == std::vector<int>{0, 255, 0, 128}));
    CHECK((bmp_pixel(*image, 0, 2) == std::vector<int>{0, 0, 255, 0}));
    CHECK((bmp_pixel(*image, 0, 3) == std::vector<int>{255, 255, 255, 64}));
    CHECK((bmp_pixel(*image, 1, 2) == std::vector<int>{255, 255, 0, 255}));

    // The same pixels without the alpha mask are opaque; other masks are refused.
    std::string bytes = *read_text_file(fixture_root() + "/backdrops/fixture_land.bmp");
    constexpr std::size_t alpha_mask = 66;
    constexpr std::size_t red_mask = 54;
    std::string no_alpha = bytes;
    no_alpha.replace(alpha_mask, 4, std::string(4, '\0'));
    const std::optional<rgba_image> opaque = parse_bmp(no_alpha);
    REQUIRE(opaque.has_value());
    CHECK((bmp_pixel(*opaque, 0, 2) == std::vector<int>{0, 0, 255, 255}));
    bytes.replace(red_mask, 4, std::string(4, '\0'));
    CHECK(!parse_bmp(bytes));
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

    const course_definition one_hole = course_of({"test"});
    CHECK(!parse_course_world_from_text(
        R"({"hole_starts": [{"hole_index": 4, "position": [0, 0, 0]}]})", one_hole));
    CHECK(!parse_course_world_from_text(R"({"hole_starts": []})", one_hole));
    const course_definition two_holes = course_of({"test", "test2"});
    CHECK(!parse_course_world_from_text(R"({"hole_starts": [
        {"hole_index": 0, "position": [0, 0, 0]},
        {"hole_index": 0, "position": [9, 0, 0]}]})", two_holes));
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

    const course_definition one_hole = course_of({"test"});
    const std::string start = R"("hole_starts": [{"hole_index": 0, "position": [0, 0, 0]}])";
    const auto with_ground = [&](const std::string& ground) { return "{" + start + ground + "}"; };
    const std::optional<course_world_definition> parsed = parse_course_world_from_text(
        with_ground(R"(, "ground": {"origin": [5, 7], "cell_size": 10, "columns": 2, "rows": 2, "heights_cm": [[0, 100], [200, 100]]})"), one_hole);
    REQUIRE(parsed.has_value());
    CHECK(near(sample_height_grid(parsed->ground, 10.0f, 12.0f), 1.5f));
    CHECK(!parse_course_world_from_text(with_ground(""), one_hole));
    CHECK(!parse_course_world_from_text(
        with_ground(R"(, "ground": {"origin": [0, 0], "cell_size": 10, "columns": 2, "rows": 2, "heights_cm": [[0, 100], [200]]})"), one_hole));
}

TEST_CASE("every shipped course, hole, world and image loads") {
    const game_content& content = shipped_content();
    // A course file the loader refuses would drop out of the menus silently.
    CHECK(content.courses.size() == json_files_in_directory(std::filesystem::path(content.asset_root) / "courses").size());
    const std::optional<rgba_image> grass = load_bmp_file(std::filesystem::path(content.asset_root) / "textures" / "rough_grass.bmp");
    CHECK(grass.has_value());
    for (const course_definition& course : content.courses) {
        // The land panorama shows the sky above it and the far ground (never
        // fully hazed) along its bottom.
        const std::optional<rgba_image> sky = load_bmp_file(std::filesystem::path(content.asset_root) / course.backdrop.sky);
        const std::optional<rgba_image> land = load_bmp_file(std::filesystem::path(content.asset_root) / course.backdrop.land);
        REQUIRE(sky.has_value());
        REQUIRE(land.has_value());
        CHECK(land->width == sky->width);
        CHECK(land->height == sky->height);
        CHECK(bmp_pixel(*land, land->height - 1, 0)[3] == 0);
        CHECK(bmp_pixel(*land, 0, 0)[3] > 0);
        CHECK(bmp_pixel(*land, 0, 0)[3] < 255);
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

// The longest course name, which the online menus and HUD show.
std::string longest_course_name() {
    std::string longest;
    for (const course_definition& course : shipped_content().courses) {
        if (course.name.size() > longest.size()) {
            longest = course.name;
        }
    }
    return longest;
}

// Every string the online screens show in their message line: server
// errors, link results and the menus' own.
std::vector<std::string> online_messages(const text_assets& text) {
    std::vector<std::string> messages;
    for (const auto& [key, value] : text.strings.entries) {
        if (key.rfind("online.error.", 0) == 0 || key.rfind("online.link.", 0) == 0 || key == text_online_room_lost) {
            messages.push_back(key);
        }
    }
    return messages;
}

// Signed in with the longest name, a link code and a few minutes on it.
online_menu_status busy_online(const std::size_t name_length) {
    online_menu_status online;
    online.available = true;
    online.status = net_status::connected;
    online.account_id = 7;
    online.name = std::string(name_length, 'W');
    online.link_code = std::string(static_cast<std::size_t>(link_code_length), 'W');
    online.link_code_minutes_left = 99;
    return online;
}

// Every online status the sign-in screen tells apart.
std::vector<online_menu_status> login_statuses() {
    std::vector<online_menu_status> statuses(6);
    statuses[1].available = true;
    statuses[2].available = true;
    statuses[2].status = net_status::failed;
    statuses[2].failure_id = "sign_in_timed_out";
    statuses[3].available = true;
    statuses[3].status = net_status::waiting_for_browser;
    statuses[4].available = true;
    statuses[4].status = net_status::signing_in;
    statuses[5].available = true;
    statuses[5].status = net_status::connecting;
    return statuses;
}

void check_fits(const overlay_grid& grid,
                const text_assets& text,
                const startup_flow_state& state,
                const startup_catalog& catalog,
                const online_menu_status& online) {
    overlay_batch batch;
    batch.grid = grid;
    draw_startup_menu(batch, text, make_startup_menu_render_data(state, catalog, text, online));
    CHECK(batch.truncated_text_count == 0U);
}

// Every in-round HUD element at once, with the widest values the game shows.
render_data busy_hud(const text_assets& text, const std::string& club_label) {
    render_data data;
    data.mode_label = format_text(text, text_hud_mode_online,
                                  {{"course", longest_course_name()},
                                   {"room", "99999"},
                                   {"players", std::to_string(shipped_content().tuning.server.room_capacity)},
                                   {"capacity", std::to_string(shipped_content().tuning.server.room_capacity)}});
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

    // Online: a full group of the longest names, over par everywhere, and
    // their name tags; the G key.
    const std::size_t longest_name = static_cast<std::size_t>(shipped_content().tuning.server.name_max_length);
    const std::array<player_relationship, 2> relationships{player_relationship::unknown, player_relationship::grouped};
    data.show_scorecard = true;
    data.scorecard = played_scorecard(shipped_content().courses.front(), text);
    for (int i = 0; i < shipped_content().tuning.server.group_capacity; ++i) {
        const std::string name(longest_name, 'W');
        data.group_scorecard.push_back(group_scorecard_row{name, 99, 999, format_relative_score(text.strings, 99), i == 0});
        data.name_tags.push_back(render_name_tag{glm::vec3(0.0f), name, relationships[static_cast<std::size_t>(i) % relationships.size()]});
    }
    data.controls.show_group_key = true;
    return data;
}
}

TEST_CASE("the shipped hole sign's board has its face texture's shape") {
    const hole_sign_tuning& sign = shipped_content().tuning.hole_sign;
    CHECK(near(sign.board_width / sign.board_height,
               static_cast<float>(hole_sign_face_width) / static_cast<float>(hole_sign_face_height), 0.001f));
}

TEST_CASE("every shipped hole sign fits its number, par and a four-digit length") {
    const text_assets& text = shipped_text_assets();
    const play_area area = hole_area(straight_hole(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 300.0f), 30.0f),
                                           shipped_content().tuning);
    REQUIRE(area.signs.size() == 1U);
    for (const course_definition& course : shipped_content().courses) {
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(shipped_content().asset_root, course, i));
            REQUIRE(hole.has_value());
            overlay_batch batch;
            draw_hole_sign_face(batch, text, make_hole_sign_labels(text, static_cast<int>(i) + 1, hole->par, 9999), area,
                                area.signs.front());
            CHECK(batch.truncated_text_count == 0U);
        }
    }
}

TEST_CASE("every shipped screen fits its text without cutting any off") {
    const text_assets& text = shipped_text_assets();
    const startup_catalog catalog = load_startup_catalog(shipped_content(), text);
    const online_menu_status online = busy_online(catalog.name_max_length);
    for (const overlay_grid& grid : shipped_grids) {
        for (const startup_flow flow : {startup_flow::main, startup_flow::help, startup_flow::course_picker, startup_flow::link_code_show,
                                        startup_flow::online_course_picker}) {
            startup_flow_state state;
            state.flow = flow;
            check_fits(grid, text, state, catalog, online_menu_status{});
            check_fits(grid, text, state, catalog, online);
        }
        for (const online_menu_status& status : login_statuses()) {
            startup_flow_state state;
            open_online_login(state);
            check_fits(grid, text, state, catalog, status);
        }

        // A full field, and every message the server or the menus can show.
        for (const startup_flow flow : {startup_flow::main, startup_flow::name_entry, startup_flow::link_code_entry,
                                        startup_flow::link_code_show, startup_flow::online_course_picker,
                                        startup_flow::joining}) {
            for (const std::string& message : online_messages(text)) {
                startup_flow_state state;
                state.flow = flow;
                state.message_key = message;
                state.field.max_length = catalog.name_max_length;
                state.field.value = std::string(catalog.name_max_length, 'W');
                state.joining_course.name = longest_course_name();
                online_menu_status status = online;
                if (flow == startup_flow::link_code_show) {
                    status.link_code.clear();
                }
                check_fits(grid, text, state, catalog, status);
            }
        }

        startup_flow_state confirm;
        open_confirm_menu(confirm);
        overlay_batch confirm_batch;
        confirm_batch.grid = grid;
        draw_startup_menu(confirm_batch, text, make_confirm_menu_render_data(confirm, text, catalog.settings));
        CHECK(confirm_batch.truncated_text_count == 0U);

        // Settings from the main menu and in a round, at every setting's
        // widest values.
        for (const bool lowest : {true, false}) {
            startup_flow_state settings;
            settings.flow = startup_flow::settings;
            for (const setting_definition& definition : catalog.settings) {
                settings.settings[definition.id] = lowest ? definition.min : definition.max;
            }
            check_fits(grid, text, settings, catalog, online);
            open_confirm_menu(settings);
            settings.confirm_settings = true;
            overlay_batch settings_batch;
            settings_batch.grid = grid;
            draw_startup_menu(settings_batch, text, make_confirm_menu_render_data(settings, text, catalog.settings));
            CHECK(settings_batch.truncated_text_count == 0U);
        }

        for (const club_definition& club : shipped_content().clubs) {
            overlay_batch batch;
            batch.grid = grid;
            draw_hud(batch, text, busy_hud(text, club.label), glm::mat4(1.0f));
            CHECK(batch.truncated_text_count == 0U);
        }

        // Every refusal the server can send, and waiting for the group, as the notice.
        std::vector<std::string> notices = online_messages(text);
        notices.push_back(text_scorecard_waiting_for_group);
        for (const std::string& message : notices) {
            render_data data = busy_hud(text, shipped_content().clubs.front().label);
            data.notice_label = lookup_text(text, message.c_str());
            overlay_batch batch;
            batch.grid = grid;
            draw_hud(batch, text, data, glm::mat4(1.0f));
            CHECK(batch.truncated_text_count == 0U);
        }

        for (const course_definition& course : shipped_content().courses) {
            for (const bool online_round : {false, true}) {
                scorecard_data scorecard = played_scorecard(course, text);
                scorecard.next_round = online_round;
                overlay_batch compact;
                compact.grid = grid;
                draw_compact_scorecard(compact, text, scorecard);
                CHECK(compact.truncated_text_count == 0U);

                overlay_batch results;
                results.grid = grid;
                draw_course_results(results, text, scorecard, {});
                CHECK(results.truncated_text_count == 0U);
                if (online_round) {
                    overlay_batch group_results;
                    group_results.grid = grid;
                    draw_course_results(group_results, text, scorecard, busy_hud(text, shipped_content().clubs.front().label).group_scorecard);
                    CHECK(group_results.truncated_text_count == 0U);
                }
            }
        }

        overlay_batch loading;
        loading.grid = grid;
        draw_loading_screen(loading, text);
        CHECK(loading.truncated_text_count == 0U);
    }
}

TEST_CASE("the shipped online config loads, and dev overrides replace only what they set") {
    const std::optional<std::string> text = read_text_file(asset_root() + "/" + online_config_path);
    REQUIRE(text.has_value());
    const std::optional<online_config> config = parse_online_config_from_text(*text);
    REQUIRE(config.has_value());
    CHECK(!config->server_uri.empty());
    CHECK(config->auth_scopes.find("openid") != std::string::npos);
    CHECK(!config->anonymous);

    const online_config local = with_overrides(*config, "http://localhost:3000", "", true);
    CHECK(local.server_uri == "http://localhost:3000");
    CHECK(local.database == config->database);
    CHECK(local.anonymous);
    CHECK(!parse_online_config_from_text("{\"server_uri\": \"x\"}").has_value());
}

