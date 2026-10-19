#include "doctest.h"

#include "net/online_config.h"

#include "core/startup_flow.h"
#include "core/vcr_frame.h"
#include "game/club_loader.h"
#include "game/content_files.h"
#include "game/course_loader.h"
#include "game/course_world_loader.h"
#include "game/hole_sign.h"
#include "game/hole_loader.h"
#include "game/lighting_loader.h"
#include "game/json_util.h"
#include "game/net_types.h"
#include "game/picture_loader.h"
#include "game/reward_rules.h"
#include "game/scorecard.h"
#include "game/sky_clock.h"
#include "game/sun_lighting.h"
#include "game/text_assets.h"
#include "game/text_ids.h"
#include "game/tuning_loader.h"
#include "physics/ground_mesh.h"
#include "physics/vector_math.h"
#include "game/play_area.h"
#include "renderer/bmp_image.h"
#include "renderer/course_map_overlay.h"
#include "renderer/hole_sign_face.h"
#include "renderer/hud_overlay.h"
#include "renderer/menu_overlay.h"
#include "renderer/overlay_batch.h"
#include "renderer/overlay_raster.h"
#include "renderer/picture_size.h"
#include "renderer/pixel_font.h"
#include "renderer/scorecard_overlay.h"
#include "renderer/ui_rect.h"
#include "renderer/vcr_overlay.h"

#include "test_support.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vector_relational.hpp>

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
    CHECK(empty.error.find("terrain.relief_scale") != std::string::npos);
    CHECK(empty.error.find("terrain.green_relief_scale") != std::string::npos);
    CHECK(empty.error.find("terrain.green_relief_blend_distance") != std::string::npos);
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

TEST_CASE("the shipped sky clock runs from its anchor date, every course on its own date") {
    const game_tuning& tuning = shipped_content().tuning;
    CHECK(tuning.sky_clock.anchor_unix_ms % 86400000LL == 0);
    CHECK(tuning.sky_clock.anchor_unix_ms > 0);
    CHECK(tuning.sky_clock.anchor_game_day >= 0.0);
    CHECK(tuning.sky_clock.anchor_game_day < 1.0);
    CHECK(tuning.sky_clock.game_days_per_real_day > 0.0);
    CHECK(tuning.sky_clock.year == 1989);

    std::string bad_date = *read_text_file(asset_root() + "/tuning/game_tuning.json");
    const std::size_t at = bad_date.find("\"anchor_real_date\": \"");
    REQUIRE(at != std::string::npos);
    bad_date.replace(at + 21, 10, "2026-13-01");
    const game_tuning_parse_result result = parse_game_tuning_from_text(bad_date);
    CHECK(!result.tuning.has_value());
    CHECK(result.error.find("sky_clock.anchor_real_date") != std::string::npos);

    // The Danish courses are played at midsummer.
    const std::vector<course_definition> courses = load_courses_from_directory(asset_root() + "/courses");
    REQUIRE(!courses.empty());
    for (const course_definition& course : courses) {
        CHECK(course.sky.month == 6);
        CHECK(course.sky.day == 21);
        CHECK(course.sky.utc_offset_hours == 2.0f);
        CHECK(course.sky.lowest_sun_deg == -90.0f);
    }
}

TEST_CASE("the shipped lighting loads, and its daylight shows the scene as it was drawn") {
    const std::optional<lighting_palette> palette = load_lighting(asset_root());
    REQUIRE(palette.has_value());
    CHECK(palette->keys.front().sun_elevation_deg <= -18.0f);
    // From the last key up, unlit things show their colours as drawn, under
    // each course's own sky and haze.
    const lighting_key day = lighting_at(*palette, 60.0f);
    CHECK(near(day.ambient_sky + day.sun_color, glm::vec3(1.0f), 0.0001f));
    CHECK(day.sky_blend == 0.0f);
    CHECK(day.haze_tint == glm::vec3(1.0f));
    // The camcorder records daylight as it is: no exposure shift, cast,
    // gain-up noise or lost colour.
    CHECK(day.exposure == 1.0f);
    CHECK(day.white_balance == glm::vec3(1.0f));
    CHECK(day.gain_noise == 0.0f);
    CHECK(day.saturation == 1.0f);
    // Night is darker than day, everywhere.
    const lighting_key night = lighting_at(*palette, -90.0f);
    CHECK(glm::all(glm::lessThan(night.ambient_sky, day.ambient_sky)));
    CHECK(glm::all(glm::lessThan(night.ambient_ground, day.ambient_ground)));
    CHECK(glm::all(glm::lessThan(night.sky_zenith, day.sky_zenith)));
    CHECK(glm::all(glm::lessThan(night.haze_tint, day.haze_tint)));
    // And grainier, with less colour, the camera's gain turned up.
    CHECK(night.gain_noise > day.gain_noise);
    CHECK(night.saturation < day.saturation);
    // The sun is the one thing far past white, for the camcorder to clip, bloom and smear.
    CHECK(palette->sky.sun_brightness > 1.0f);
}

namespace {
float luminance(const glm::vec3& color) {
    return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
}

// The brighter's luminance over the darker's, each with a little flare.
float luminance_contrast(const glm::vec3& a, const glm::vec3& b) {
    const float flare = 0.05f;
    const float high = std::max(luminance(a), luminance(b)) + flare;
    const float low = std::min(luminance(a), luminance(b)) + flare;
    return high / low;
}
}

TEST_CASE("the ball stands out on the fairway and the flag against the sky at every time of day") {
    constexpr float min_readable_contrast = 1.6f;
    const std::optional<lighting_palette> palette = load_lighting(asset_root());
    REQUIRE(palette.has_value());
    const scene_colors& colors = palette->colors;
    for (const lighting_key& key : palette->keys) {
        // The most light on open ground (lighting.glsl): the sky's from above
        // and the sun's at most. The ball lying there takes the same light,
        // the flag its share of it, each never below its glow floor.
        const glm::vec3 light = glm::min(key.ambient_sky + key.sun_color, glm::vec3(1.0f));
        const glm::vec3 fairway = colors.ground.fairway * light;
        const glm::vec3 ball = colors.markers.ball * glm::max(light, glm::vec3(colors.glow.ball));
        CHECK(luminance_contrast(ball, fairway) >= min_readable_contrast);
        const glm::vec3 flag = colors.markers.flag * glm::max(light, glm::vec3(colors.glow.flag));
        for (const course_definition& course : shipped_content().courses) {
            // The sky at the horizon: the course's haze, giving way to the
            // palette's tint of it as far as the key's sky blend (backdrop.frag).
            const glm::vec3 haze = course.backdrop.haze_color;
            const glm::vec3 sky = glm::mix(haze, haze * key.haze_tint, key.sky_blend);
            CHECK(luminance_contrast(flag, sky) >= min_readable_contrast);
        }
    }
}

TEST_CASE("at night the ball glows past white and the green's floodlights lift the cup off its green") {
    constexpr float min_readable_contrast = 1.6f;
    const std::optional<lighting_palette> palette = load_lighting(asset_root());
    REQUIRE(palette.has_value());
    const scene_colors& colors = palette->colors;
    const night_light_style& lights = palette->night_lights;
    const lighting_key night = lighting_at(*palette, -90.0f);
    REQUIRE(night_light_share(lights, night.sun_elevation_deg) == 1.0f);
    // Past white, for the camcorder to bloom; and the lamps' faces too.
    const glm::vec3 ball = colors.markers.ball;
    CHECK(night_glow_floors(colors.glow, lights, 1.0f).ball * std::max({ball.r, ball.g, ball.b}) > 1.0f);
    CHECK(luminance(lights.floodlight.color * lights.lamp_glow) > 1.0f);

    // Every green's floodlights on its cup (lighting.glsl's light_reaching),
    // as the shipped tuning stands them round the fixture holes: on the green,
    // which faces up, and on the cup, which takes them unlit.
    game_state game = started_game(fixture_hub_course());
    refresh_static_anchor_cache(game);
    REQUIRE(game.static_anchors.hub_pin_markers.size() == game.course_holes.size());
    const auto smooth = [](const float edge0, const float edge1, const float x) {
        const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float cone_edge = std::cos(glm::radians(lights.floodlight_cone_deg));
    const float cone_full = std::cos(glm::radians(lights.floodlight_cone_deg - lights.floodlight_soft_deg));
    const glm::vec3 sky = glm::min(night.ambient_sky + night.sun_color, glm::vec3(1.0f));
    for (std::size_t hole = 0; hole < game.course_holes.size(); ++hole) {
        const glm::vec3 pin = game.static_anchors.hub_pin_markers[hole];
        glm::vec3 on_green(0.0f);
        glm::vec3 on_cup(0.0f);
        int green_lamps = 0;
        for (const floodlight& lamp : game.area.floodlights) {
            if (lamp.area_hole != hole || lamp.target != floodlight_target::green) {
                continue;
            }
            ++green_lamps;
            const glm::vec3 to = lamp.lamp - pin;
            const glm::vec3 towards = glm::normalize(to);
            const float reach = std::clamp(1.0f - glm::dot(to, to) / (lights.floodlight.radius * lights.floodlight.radius), 0.0f, 1.0f);
            const glm::vec3 light = lights.floodlight.color * lights.floodlight.intensity * reach * reach *
                smooth(cone_edge, cone_full, glm::dot(-towards, lamp.aim));
            on_cup += light;
            on_green += light * std::max((towards.y + 0.25f) / 1.25f, 0.0f);
        }
        CHECK(green_lamps >= 1);
        const glm::vec3 green = colors.ground.green * (night.ambient_sky + on_green);
        const glm::vec3 cup = colors.markers.cup * (glm::max(sky, glm::vec3(colors.glow.cup)) + on_cup);
        CHECK(luminance(green) > luminance(colors.ground.green * night.ambient_sky) * 2.0f);
        CHECK(luminance_contrast(green, cup) >= 2.0f * min_readable_contrast);
        // And the flag over it.
        const glm::vec3 flag = colors.markers.flag * (glm::max(sky, glm::vec3(colors.glow.flag)) + on_cup);
        CHECK(luminance_contrast(flag, colors.ground.green * night.ambient_sky) >= min_readable_contrast);
    }
}

TEST_CASE("the shipped bag is in order with labels and a rising power ladder") {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    const std::vector<std::string> ids{"putter", "sand_wedge", "pitching_wedge", "nine_iron", "seven_iron",
                                       "five_iron", "seven_wood", "four_wood", "driver"};
    const std::vector<std::string> labels{"P", "SW", "PW", "9I", "7I", "5I", "7W", "4W", "DR"};
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

// Lit pixels that differ between two glyphs, the narrower padded on the right.
int glyph_difference(const pixel_font_data& font, const char32_t a, const char32_t b) {
    const pixel_glyph& left = find_glyph(font, a);
    const pixel_glyph& right = find_glyph(font, b);
    const int width = std::max(left.width, right.width);
    const auto lit = [&font](const pixel_glyph& glyph, const int x, const int y) {
        return x < glyph.width && glyph.pixels[static_cast<std::size_t>(y * glyph.width + x)] != 0;
    };
    int difference = 0;
    for (int y = 0; y < font.height; ++y) {
        for (int x = 0; x < width; ++x) {
            difference += lit(left, x, y) != lit(right, x, y) ? 1 : 0;
        }
    }
    return difference;
}

constexpr int least_glyph_difference = 3;

TEST_CASE("the shipped font tells look-alike characters apart") {
    const pixel_font_data& font = shipped_text_assets().font;
    const std::vector<std::pair<char32_t, char32_t>> look_alikes{
        {U'S', U'5'}, {U'S', U'8'}, {U'S', U'6'}, {U'O', U'0'}, {U'D', U'0'}, {U'D', U'O'}, {U'O', U'Q'},
        {U'Q', U'0'}, {U'B', U'8'}, {U'Z', U'2'}, {U'I', U'1'}, {U'G', U'6'}, {U'5', U'6'}, {U'6', U'8'},
        {U'U', U'V'}, {U'U', U'O'}, {U'O', U'\u00D8'}, {U'0', U'\u00D8'}, {U'E', U'F'}, {U'P', U'R'},
        {U'F', U'P'}, {U'B', U'D'}, {U'H', U'4'}, {U'C', U'G'}, {U'3', U'8'}, {U'9', U'8'}, {U'7', U'1'}};
    for (const auto& [a, b] : look_alikes) {
        REQUIRE(font_has_glyph(font, a));
        REQUIRE(font_has_glyph(font, b));
        CHECK(glyph_difference(font, a, b) >= least_glyph_difference);
    }
}

TEST_CASE("the shipped clubs' labels are unique and can't be misread as each other") {
    const pixel_font_data& font = shipped_text_assets().font;
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    for (std::size_t i = 0; i < clubs.size(); ++i) {
        for (std::size_t j = i + 1; j < clubs.size(); ++j) {
            const std::string& a = clubs[i].label;
            const std::string& b = clubs[j].label;
            CHECK(a != b);
            if (a.size() != b.size()) {
                continue;
            }
            bool told_apart = false;
            for (std::size_t c = 0; c < a.size(); ++c) {
                told_apart = told_apart || glyph_difference(font, static_cast<char32_t>(a[c]), static_cast<char32_t>(b[c])) >= least_glyph_difference;
            }
            CHECK(told_apart);
        }
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
                                     rewards.cart_on_road.skill_id}) {
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
        R"("sky": {"date": "06-21", "utc_offset_hours": 2}, "backdrop": {"sky": "s.bmp", "land": "l.bmp", "haze_color": [1, 1, 1], "haze_amount": 1, "haze_distance": 500})";
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

TEST_CASE("a course's sky is a day of the year and how low its sun may sink") {
    CHECK(fixture_hub_course().sky.month == 6);
    CHECK(fixture_hub_course().sky.day == 21);
    CHECK(fixture_hub_course().sky.utc_offset_hours == 2.0f);
    CHECK(fixture_hub_course().sky.lowest_sun_deg == -90.0f);

    const std::string start =
        R"({"id": "ok", "name": "Ok", "world": "w.json", "holes": ["ok"], "backdrop": {"sky": "s.bmp", "land": "l.bmp", "haze_color": [1, 1, 1], "haze_amount": 1, "haze_distance": 500})";
    const auto sky_of = [&start](const std::string& sky) { return parse_course_from_text(start + sky + "}"); };
    const std::optional<course_definition> white_nights = sky_of(R"(, "sky": {"date": "06-24", "utc_offset_hours": 1, "lowest_sun_deg": 1.5})");
    REQUIRE(white_nights.has_value());
    CHECK(white_nights->sky.month == 6);
    CHECK(white_nights->sky.day == 24);
    CHECK(white_nights->sky.utc_offset_hours == 1.0f);
    CHECK(white_nights->sky.lowest_sun_deg == 1.5f);
    const std::optional<course_definition> winter = sky_of(R"(, "sky": {"date": "12-31", "utc_offset_hours": -5.5})");
    REQUIRE(winter.has_value());
    CHECK(winter->sky.utc_offset_hours == -5.5f);
    CHECK(winter->sky.lowest_sun_deg == -90.0f);
    CHECK(sky_of(R"(, "sky": {"date": "02-28", "utc_offset_hours": -12, "lowest_sun_deg": -90})").has_value());
    CHECK(sky_of(R"(, "sky": {"date": "01-01", "utc_offset_hours": 14, "lowest_sun_deg": 30})").has_value());

    CHECK(!sky_of(""));
    CHECK(!sky_of(R"(, "sky": {})"));
    CHECK(!sky_of(R"(, "sky": "06-21")"));
    for (const char* bad : {"6-21", "06/21", "13-01", "00-10", "06-31", "02-29", "06-00", "1989-06-21", "0a-21"}) {
        CHECK(!sky_of(std::string(R"(, "sky": {"date": ")") + bad + R"(", "utc_offset_hours": 2})"));
    }
    CHECK(!sky_of(R"(, "sky": {"date": "06-21"})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": 15})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": -13})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": "CEST"})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": 2, "lowest_sun_deg": 31})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": 2, "lowest_sun_deg": -91})"));
    CHECK(!sky_of(R"(, "sky": {"date": "06-21", "utc_offset_hours": 2, "lowest_sun_deg": "low"})"));
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

TEST_CASE("bmp images written for screenshots read back the same") {
    rgba_image image;
    image.width = 3;  // rows padded to 12 bytes
    image.height = 2;
    image.pixels = {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,
                    10, 20, 30, 255, 40, 50, 60, 0, 70, 80, 90, 128};
    const std::string bytes = encode_bmp(image);
    CHECK(bytes.size() == 54U + 2U * 12U);
    const std::optional<rgba_image> read = parse_bmp(bytes);
    REQUIRE(read.has_value());
    CHECK(read->width == 3);
    CHECK(read->height == 2);
    CHECK((bmp_pixel(*read, 0, 0) == std::vector<int>{255, 0, 0, 255}));
    CHECK((bmp_pixel(*read, 0, 2) == std::vector<int>{0, 0, 255, 255}));
    // Alpha is dropped: every pixel reads back opaque.
    CHECK((bmp_pixel(*read, 1, 1) == std::vector<int>{40, 50, 60, 255}));
    CHECK((bmp_pixel(*read, 1, 2) == std::vector<int>{70, 80, 90, 255}));
}

TEST_CASE("the picture style needs every field in range, and the shipped one loads") {
    REQUIRE(load_picture_style(asset_root()).has_value());
    CHECK(!load_picture_style(fixture_root()).has_value());
    const json shipped = *parse_json(*read_text_file(asset_root() + "/" + picture_style_path));
    const auto parses = [](const json& value) { return parse_picture_from_text(value.dump()).has_value(); };

    json lines = shipped;
    lines["target_lines"] = 240;
    const std::optional<picture_style> style = parse_picture_from_text(lines.dump());
    REQUIRE(style.has_value());
    CHECK(style->target_lines == 240);
    for (const json bad_lines : {json(0), json(360.5)}) {
        lines["target_lines"] = bad_lines;
        CHECK(!parses(lines));
    }
    CHECK(!parse_picture_from_text("not json"));

    // Every field is required, in each section.
    for (const char* section : {"camcorder", "tape", "tv"}) {
        for (const auto& [key, value] : shipped[section].items()) {
            json missing = shipped;
            missing[section].erase(key);
            CHECK(!parses(missing));
        }
    }
    for (const char* wear : {"fresh", "worn"}) {
        for (const auto& [key, value] : shipped["tape"][wear].items()) {
            json missing = shipped;
            missing["tape"][wear].erase(key);
            CHECK(!parses(missing));
        }
    }

    const auto out_of_range = [&](const std::vector<std::string>& path, const json& value) {
        json changed = shipped;
        json* field = &changed;
        for (const std::string& key : path) {
            field = &(*field)[key];
        }
        *field = value;
        return !parses(changed);
    };
    CHECK(out_of_range({"tape", "worn", "saturation"}, -0.1f));
    CHECK(out_of_range({"tape", "fresh", "luma_lines"}, 0));
    CHECK(out_of_range({"tape", "tracking_fade_seconds"}, 0));
    CHECK(out_of_range({"tape", "fresh", "grain"}, 1.5f));
    CHECK(out_of_range({"tape", "worn", "dropouts_per_second"}, -1));
    CHECK(out_of_range({"tv", "scanline_full_factor"}, 1));
    CHECK(out_of_range({"tv", "scanline_full_factor"}, 2.5f));
    CHECK(out_of_range({"tv", "curvature"}, 1));
    CHECK(out_of_range({"tv", "border_color"}, json::array({0.0, 0.0, 1.5})));
    CHECK(out_of_range({"tv", "border_color"}, json::array({0.0, 0.0})));
    CHECK(out_of_range({"camcorder", "iris_max_stops"}, -1));
    CHECK(out_of_range({"camcorder", "iris_seconds"}, 0));
    CHECK(out_of_range({"camcorder", "shoulder"}, 1.5f));
    CHECK(out_of_range({"camcorder", "shadow_tint"}, json::array({0.0, -0.1, 0.0})));
}

TEST_CASE("the camcorder clips, crushes, blooms and smears only past white, and meters within its iris") {
    const camcorder_style camera = load_picture_style(asset_root())->camcorder;
    CHECK(camera.shoulder < 1.0f);
    CHECK(camera.toe > 0.0f);
    CHECK(camera.bloom_threshold > 1.0f);
    CHECK(camera.smear_threshold > camera.bloom_threshold);
    CHECK(camera.iris_min_stops < 0.0f);
    CHECK(camera.iris_max_stops > 0.0f);
    // The tape owns colour loss and grain; the camera only adds gain-up noise.
    CHECK(camera.gain_chroma > camera.gain_grain);
}

TEST_CASE("tape wear runs the look from next to no tape to worn") {
    const picture_style picture = *load_picture_style(asset_root());
    const tape_style& tape = picture.tape;
    CHECK(tape_look_at(tape, 0.0f).grain == tape.fresh.grain);
    CHECK(tape_look_at(tape, 1.0f).dropouts_per_second == tape.worn.dropouts_per_second);
    CHECK(tape_look_at(tape, 2.0f).wobble == tape.worn.wobble);
    CHECK(tape_look_at(tape, -1.0f).saturation == tape.fresh.saturation);
    CHECK(near(tape_look_at(tape, 0.5f).chroma_noise, 0.5f * (tape.fresh.chroma_noise + tape.worn.chroma_noise)));
    // Detail blends by blur width: halfway is the harmonic mean of the lines.
    const float halfway = 2.0f / (1.0f / tape.fresh.luma_lines + 1.0f / tape.worn.luma_lines);
    CHECK(std::abs(tape_look_at(tape, 0.5f).luma_lines - halfway) < 0.01f);
    CHECK(std::abs(tape_look_at(tape, 1.0f).luma_lines - tape.worn.luma_lines) < 0.01f);
    // Fresh is next to nothing: no colour shift or tone change, detail finer
    // than the picture's lines.
    CHECK(tape.fresh.luma_lines > 4.0f * static_cast<float>(picture.target_lines));
    CHECK(tape.fresh.chroma_delay == 0.0f);
    CHECK(tape.fresh.black_lift == 0.0f);
    CHECK(tape.fresh.saturation == 1.0f);
    CHECK(tape.fresh.head_switch_noise == 0.0f);
    // Worn is VHS: colour coarser than brightness, bleeding right, washed out.
    CHECK(tape.worn.chroma_lines < tape.worn.luma_lines);
    CHECK(tape.worn.chroma_delay > 0.0f);
    CHECK(tape.worn.black_lift > 0.0f);
    CHECK(tape.worn.saturation < 1.0f);
}

TEST_CASE("the picture keeps whole window rows per line, and wider windows get more width") {
    struct expected {
        int window_width, window_height, factor, width, height, crop_x, crop_y;
    };
    for (const expected& e : std::vector<expected>{{1920, 1080, 3, 640, 360, 0, 0},
                                                   {3440, 1440, 4, 860, 360, 0, 0},
                                                   {2560, 1440, 4, 640, 360, 0, 0},
                                                   {3840, 2160, 6, 640, 360, 0, 0},
                                                   {1366, 768, 2, 683, 384, 0, 0},
                                                   {2560, 1080, 3, 854, 360, 1, 0},
                                                   {1001, 1001, 3, 334, 334, 0, 0},
                                                   {1003, 1003, 3, 335, 335, 1, 1},
                                                   {320, 200, 1, 320, 200, 0, 0},
                                                   {0, 0, 1, 1, 1, 0, 0}}) {
        const picture_size size = picture_size_for_window(e.window_width, e.window_height, 360);
        CHECK(size.factor == e.factor);
        CHECK(size.width == e.width);
        CHECK(size.height == e.height);
        CHECK(size.crop_x == e.crop_x);
        CHECK(size.crop_y == e.crop_y);
        // The scene covers the window and overhangs it by less than a scene pixel.
        CHECK(size.width * size.factor >= e.window_width);
        CHECK(size.width * size.factor - std::max(1, e.window_width) < size.factor);
    }
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

TEST_CASE("course worlds say where they are on Earth") {
    const course_definition course = fixture_hub_course();
    const std::optional<course_world_definition> world =
        load_course_world_from_file(course_world_file_path(fixture_root(), course), course);
    REQUIRE(world.has_value());
    CHECK(near(world->latitude_deg, 56.18f));
    CHECK(near(world->longitude_deg, 10.16f));

    const course_definition one_hole = course_of({"test"});
    const std::string rest = R"("hole_starts": [{"hole_index": 0, "position": [0, 0, 0]}],
        "ground": {"origin": [0, 0], "cell_size": 10, "columns": 2, "rows": 2, "heights_cm": [[0, 0], [0, 0]]})";
    const auto with_projection = [&](const std::string& projection) { return "{" + projection + rest + "}"; };
    const std::optional<course_world_definition> south =
        parse_course_world_from_text(with_projection(R"("projection": {"origin_lat": -33.9, "origin_lon": 18.4},)"), one_hole);
    REQUIRE(south.has_value());
    CHECK(near(south->latitude_deg, -33.9f));
    CHECK(near(south->longitude_deg, 18.4f));
    CHECK(!parse_course_world_from_text(with_projection(""), one_hole));
    CHECK(!parse_course_world_from_text(with_projection(R"("projection": {"origin_lat": 56.2},)"), one_hole));
    CHECK(!parse_course_world_from_text(with_projection(R"("projection": {"origin_lat": 95, "origin_lon": 10},)"), one_hole));
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
    const std::string start = R"("projection": {"origin_lat": 56.2, "origin_lon": 10.2},
        "hole_starts": [{"hole_index": 0, "position": [0, 0, 0]}])";
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
// The scene target (renderer/picture_size.h) for 4:3, 16:9 and 21:9 windows
// at the shipped line count; 4:3 is the narrowest the screens are laid out for.
std::vector<overlay_grid> shipped_grids() {
    const int lines = load_picture_style(asset_root())->target_lines;
    std::vector<overlay_grid> grids;
    for (const auto& [width, height] : std::vector<std::pair<int, int>>{{1440, 1080}, {1920, 1080}, {2560, 1080}}) {
        const picture_size size = picture_size_for_window(width, height, lines);
        grids.push_back(overlay_grid{size.width, size.height});
    }
    return grids;
}

// A scorecard for `course` with every hole played over par by `players` of
// the longest names, so every cell has its widest text.
scorecard_data played_scorecard(const course_definition& course, const text_assets& text, const int players) {
    scorecard_data scorecard;
    scorecard.course_name = course.name;
    for (const std::string& file : course.holes) {
        const std::optional<hole_data> hole = load_hole_from_file(asset_root() + "/" + file);
        CHECK(hole.has_value());
        scorecard.pars.push_back(hole ? hole->par : 0);
    }
    std::vector<int> strokes;
    for (const int par : scorecard.pars) {
        strokes.push_back(par + 10);
    }
    const std::string name(static_cast<std::size_t>(shipped_content().tuning.server.name_max_length), 'W');
    for (int i = 0; i < players; ++i) {
        scorecard.players.push_back(make_scorecard_player(text.strings, name, strokes, scorecard.pars));
        scorecard.players.back().me = players > 1 && i == 0;
    }
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
    draw_startup_menu(batch, text, make_startup_menu_render_data(state, catalog, text, online), test_scene_colors().ground);
    CHECK(batch.truncated_text_count == 0U);
}

const club_definition& longest_named_club() {
    const std::vector<club_definition>& clubs = shipped_content().clubs;
    return *std::max_element(clubs.begin(), clubs.end(), [](const club_definition& a, const club_definition& b) {
        return a.name.size() < b.name.size();
    });
}

// Every in-round HUD element at once, with the widest values the game shows.
render_data busy_hud(const text_assets& text, const club_definition& club) {
    render_data data;
    data.mode_label = format_text(text, text_hud_mode_online,
                                  {{"course", longest_course_name()},
                                   {"room", "99999"},
                                   {"players", std::to_string(shipped_content().tuning.server.room_capacity)},
                                   {"capacity", std::to_string(shipped_content().tuning.server.room_capacity)}});
    data.show_swing_clock = true;
    data.swing_power = 1.0f;
    data.driving_cart = true;
    data.selected_club_label = club.label;
    data.selected_club_name = club.name;
    data.show_skills_panel = true;
    for (const skill_definition& skill : shipped_content().skills) {
        data.skills.push_back(render_skill_progress{lookup_text(text, skill_text_key(skill.id).c_str()), 99, 9999999, 9999999});
    }
    data.xp_drops.push_back(render_xp_drop{skill_icon_id::generic, 99999, 0.5f});
    data.show_rangefinder = true;
    data.rangefinder_label = format_text(text, text_hud_rangefinder, {{"meters", "999"}});

    // Online: a full group of the longest names, over par everywhere, and
    // their name tags.
    const std::size_t longest_name = static_cast<std::size_t>(shipped_content().tuning.server.name_max_length);
    const std::array<player_relationship, 2> relationships{player_relationship::unknown, player_relationship::grouped};
    data.show_scorecard = true;
    data.scorecard = played_scorecard(shipped_content().courses.front(), text, shipped_content().tuning.server.group_capacity);
    for (int i = 0; i < shipped_content().tuning.server.group_capacity; ++i) {
        const std::string name(longest_name, 'W');
        data.name_tags.push_back(render_name_tag{glm::vec3(0.0f), name, relationships[static_cast<std::size_t>(i) % relationships.size()]});
    }
    return data;
}
}

TEST_CASE("every shipped course's scorecard shows every hole, its par and the totals, for one player and a full group") {
    const text_assets& text = shipped_text_assets();
    for (const course_definition& course : shipped_content().courses) {
        for (const int players : {1, shipped_content().tuning.server.group_capacity}) {
            const scorecard_data scorecard = played_scorecard(course, text, players);
            const std::size_t holes = scorecard.pars.size();
            const bool out_and_in = holes > 9U;
            int total_par = 0;
            for (const int par : scorecard.pars) {
                total_par += par;
            }
            for (const bool results : {false, true}) {
                const std::vector<scorecard_label> labels = scorecard_labels(text, scorecard, results);
                std::vector<std::string> hole_numbers;
                std::vector<std::string> pars;
                std::vector<std::string> par_totals;
                std::vector<std::string> headings;
                std::size_t strokes = 0;
                std::size_t stroke_totals = 0;
                std::size_t relatives = 0;
                for (const scorecard_label& label : labels) {
                    switch (label.kind) {
                        case scorecard_label_kind::hole: hole_numbers.push_back(label.text); break;
                        case scorecard_label_kind::par: pars.push_back(label.text); break;
                        case scorecard_label_kind::par_total: par_totals.push_back(label.text); break;
                        case scorecard_label_kind::heading: headings.push_back(label.text); break;
                        case scorecard_label_kind::strokes: strokes += label.text.empty() ? 0U : 1U; break;
                        case scorecard_label_kind::strokes_total:
                            stroke_totals += label.text == std::to_string(scorecard.players.front().total_strokes) ? 1U : 0U;
                            break;
                        case scorecard_label_kind::relative: relatives += label.text == scorecard.players.front().relative_label ? 1U : 0U; break;
                        case scorecard_label_kind::card:
                        case scorecard_label_kind::name: break;
                    }
                }
                REQUIRE(hole_numbers.size() == holes);
                for (std::size_t i = 0; i < holes; ++i) {
                    CHECK(hole_numbers[i] == std::to_string(i + 1U));
                    CHECK(pars[i] == std::to_string(scorecard.pars[i]));
                }
                CHECK(strokes == holes * static_cast<std::size_t>(players));
                CHECK(stroke_totals == static_cast<std::size_t>(players));
                CHECK(relatives == static_cast<std::size_t>(players));
                CHECK(std::find(par_totals.begin(), par_totals.end(), std::to_string(total_par)) != par_totals.end());
                CHECK(std::count(headings.begin(), headings.end(), lookup_text(text, text_scorecard_total)) == 1);
                CHECK(std::count(headings.begin(), headings.end(), lookup_text(text, text_scorecard_out)) == (out_and_in ? 1 : 0));
                CHECK(std::count(headings.begin(), headings.end(), lookup_text(text, text_scorecard_in)) == (out_and_in ? 1 : 0));
                CHECK(par_totals.size() == (out_and_in ? 3U : 1U));

                // Every label in its own box on the screen, legible at every aspect.
                for (std::size_t i = 0; i < labels.size(); ++i) {
                    const ui_rect& box = labels[i].box;
                    CHECK(rect_left(box) >= -1.0f);
                    CHECK(rect_right(box) <= 1.0f);
                    CHECK(rect_bottom(box) >= -1.0f);
                    CHECK(rect_top(box) <= 1.0f);
                    for (std::size_t j = i + 1U; j < labels.size(); ++j) {
                        const ui_rect& other = labels[j].box;
                        const float overlap_x = std::min(rect_right(box), rect_right(other)) - std::max(rect_left(box), rect_left(other));
                        const float overlap_y = std::min(rect_top(box), rect_top(other)) - std::max(rect_bottom(box), rect_bottom(other));
                        CHECK((overlap_x <= 1e-4f || overlap_y <= 1e-4f));
                    }
                    for (const overlay_grid& grid : shipped_grids()) {
                        CHECK((labels[i].text.empty() || fit_text_scale(text.font, labels[i].style, labels[i].text, box, grid) >= 1));
                    }
                }
            }
        }
    }
}

TEST_CASE("the shipped hole sign's board has its face texture's shape") {
    const hole_sign_tuning& sign = shipped_content().tuning.hole_sign;
    CHECK(near(sign.board_width / sign.board_height,
               static_cast<float>(hole_sign_face_width) / static_cast<float>(hole_sign_face_height), 0.001f));
}

TEST_CASE("every shipped course's map numbers each hole in the middle of the hole, legible and clear of the others") {
    const game_content& content = shipped_content();
    const text_assets& text = shipped_text_assets();
    for (const course_definition& course : content.courses) {
        const std::optional<course_world_definition> world =
            load_course_world_from_file(course_world_file_path(content.asset_root, course), course);
        REQUIRE(world.has_value());
        REQUIRE(world->hole_starts.size() == course.holes.size());
        render_data data;
        glm::vec3 low(std::numeric_limits<float>::max());
        glm::vec3 high(std::numeric_limits<float>::lowest());
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(content.asset_root, course, i));
            REQUIRE(hole.has_value());
            const std::vector<glm::vec3> line = line_of_play(place_hole(*hole, world->hole_starts[i]));
            // The map fits the holes' ribbons (play_area::holes_low/high).
            const glm::vec3 reach(std::max(hole->spline.width, hole->spline.rough_width) * 0.5f);
            for (const glm::vec3& point : line) {
                low = glm::min(low, point - reach);
                high = glm::max(high, point + reach);
            }
            data.map_holes.push_back(render_map_hole{std::to_string(i + 1), line});
        }
        data.course_map_low = low;
        data.course_map_high = high;
        data.player_position = data.map_holes.front().line_of_play.front();
        for (const overlay_grid& grid : shipped_grids()) {
            const course_map_layout layout = make_course_map_layout(data, grid);
            const std::vector<ui_rect> boxes = course_map_number_boxes(layout, grid, data);
            REQUIRE(boxes.size() == data.map_holes.size());
            const glm::vec2 pixels(static_cast<float>(grid.width) * 0.5f, static_cast<float>(grid.height) * 0.5f);
            for (std::size_t i = 0; i < boxes.size(); ++i) {
                // On the hole's line of play, in its middle half.
                const std::vector<glm::vec3>& line = data.map_holes[i].line_of_play;
                const float length = polyline_length(line);
                float off_line = std::numeric_limits<float>::max();
                for (int step = 25; step <= 75; ++step) {
                    const glm::vec2 point = map_point(layout, point_along(line, length * static_cast<float>(step) / 100.0f));
                    off_line = std::min(off_line, glm::length((point - boxes[i].center) * pixels));
                }
                CHECK(off_line < 2.0f);
                for (std::size_t j = 0; j < i; ++j) {
                    const glm::vec2 gap = glm::abs(boxes[i].center - boxes[j].center) - (boxes[i].half_size + boxes[j].half_size);
                    CHECK((gap.x >= 0.0f || gap.y >= 0.0f));
                }
            }
            overlay_batch batch;
            batch.grid = grid;
            draw_course_map_marks(batch, text, layout, data);
            CHECK(batch.truncated_text_count == 0U);
        }
    }
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
                                area.signs.front(), test_scene_colors().ground);
            CHECK(batch.truncated_text_count == 0U);
        }
    }
}

TEST_CASE("every shipped hole sign's map frames its hole and shows what lies on it") {
    const game_content& content = shipped_content();
    const text_assets& text = shipped_text_assets();
    const ground_colors& ground = test_scene_colors().ground;
    // How far past the hole's fairway and rough the frame may reach on either
    // side along its tighter axis (for the pin's green).
    constexpr float slack = 30.0f;
    constexpr int margin = 1;
    for (const course_definition& course : content.courses) {
        const std::optional<course_world_definition> world =
            load_course_world_from_file(course_world_file_path(content.asset_root, course), course);
        REQUIRE(world.has_value());
        std::vector<hole_data> holes;
        for (std::size_t i = 0; i < course.holes.size(); ++i) {
            const std::optional<hole_data> hole = load_hole_from_file(course_hole_path(content.asset_root, course, i));
            REQUIRE(hole.has_value());
            holes.push_back(*hole);
        }
        // What a sign's face is drawn from: the placed holes and their signs.
        play_area area = place_course_holes(holes, *world, content.tuning);
        std::vector<hole_data> placed;
        for (std::size_t i = 0; i < holes.size(); ++i) {
            placed.push_back(place_hole(holes[i], world->hole_starts[i]));
        }
        area.signs = place_hole_signs(area, placed, content.tuning.hole_sign);
        REQUIRE(area.signs.size() == holes.size());
        for (const hole_sign& sign : area.signs) {
            // The pin is on the hole's own ground, and so is its line of play.
            for (const glm::vec3& point : {point_along(sign.line_of_play, sign.length * 0.5f), sign.line_of_play.back()}) {
                const std::optional<hole_sign_map_spot> spot = hole_sign_map_spot_at(area, sign, point);
                CHECK((spot.has_value() && spot->own_hole));
            }
            const hole_sign_map_frame frame = make_hole_sign_map_frame(area, sign);
            const auto in_box = [](const glm::ivec2 texel, const int inset) {
                return texel.x >= inset && texel.y >= inset && texel.x < hole_sign_map_width - inset &&
                       texel.y < hole_sign_map_height - inset;
            };
            // The whole line of play, with its fairway and rough, on the map.
            glm::vec2 low(std::numeric_limits<float>::max());
            glm::vec2 high(std::numeric_limits<float>::lowest());
            std::vector<glm::vec3> samples;
            for (float along = 0.0f; along < sign.length; along += 2.0f) {
                samples.push_back(point_along(sign.line_of_play, along));
            }
            samples.push_back(sign.line_of_play.back());
            for (const glm::vec3& point : samples) {
                CHECK(in_box(frame.texel(point), margin));
                for (const float side : {-1.0f, 1.0f}) {
                    CHECK(in_box(frame.texel(point + frame.far_side * (side * sign.half_width * 0.9f)), 0));
                    for (const glm::vec3& axis : {frame.to_pin, frame.far_side}) {
                        const glm::vec3 offset = horizontal(point + axis * (side * sign.half_width) - frame.origin);
                        const glm::vec2 local(glm::dot(offset, frame.to_pin), glm::dot(offset, frame.far_side));
                        low = glm::min(low, local);
                        high = glm::max(high, local);
                    }
                }
            }
            // Nothing far off the hole stretches it: along one of its axes the
            // box, less its margin, is the hole's own corridor and a little more.
            const glm::vec2 box(static_cast<float>(hole_sign_map_width), static_cast<float>(hole_sign_map_height));
            const glm::vec2 seen = (box - glm::vec2(4.0f)) / frame.texels_per_unit;
            const glm::vec2 corridor = high - low + glm::vec2(2.0f * slack);
            CHECK((seen.x <= corridor.x || seen.y <= corridor.y));

            // Every green, bunker and pond on the hole's corridor is on the map
            // in its own ink, whichever hole it was mapped with.
            overlay_batch batch;
            draw_hole_sign_face(batch, text, make_hole_sign_labels(text, 1, 3, 100), area, sign, ground);
            const rgba_image face = rasterize_overlay_batch(batch, test_scene_colors().fixtures.sign_board);
            const auto drawn = [&](const glm::vec3& ink) {
                for (std::size_t i = 0; i + 3 < face.pixels.size(); i += 4) {
                    if (face.pixels[i] == std::lround(ink.r * 255.0f) && face.pixels[i + 1] == std::lround(ink.g * 255.0f) &&
                        face.pixels[i + 2] == std::lround(ink.b * 255.0f)) {
                        return true;
                    }
                }
                return false;
            };
            for (const material_zone& zone : area.zones) {
                float off_line = std::numeric_limits<float>::max();
                for (const glm::vec3& point : samples) {
                    off_line = std::min(off_line, horizontal_distance(point, zone.center));
                }
                if (off_line > sign.half_width) {
                    continue;
                }
                CHECK(in_box(frame.texel(zone.center), 0));
                const std::optional<terrain_material> there = zone_material_at(area.zones, zone.center);
                const std::optional<hole_sign_map_spot> spot = hole_sign_map_spot_at(area, sign, zone.center);
                REQUIRE(there.has_value());
                REQUIRE(spot.has_value());
                CHECK(spot->material == *there);
                CHECK(drawn(hole_sign_map_ink(ground, *spot)));
            }
        }
    }
}

TEST_CASE("every shipped screen fits its text without cutting any off") {
    const text_assets& text = shipped_text_assets();
    const startup_catalog catalog = load_startup_catalog(shipped_content(), text);
    const online_menu_status online = busy_online(catalog.name_max_length);
    for (const overlay_grid& grid : shipped_grids()) {
        for (const startup_flow flow : {startup_flow::main, startup_flow::help, startup_flow::course_picker, startup_flow::exit_prompt,
                                        startup_flow::link_code_show,
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
        draw_startup_menu(confirm_batch, text, make_confirm_menu_render_data(confirm, text, catalog.settings),
                          test_scene_colors().ground);
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
            draw_startup_menu(settings_batch, text, make_confirm_menu_render_data(settings, text, catalog.settings),
                              test_scene_colors().ground);
            CHECK(settings_batch.truncated_text_count == 0U);
        }

        for (const club_definition& club : shipped_content().clubs) {
            overlay_batch batch;
            batch.grid = grid;
            draw_hud(batch, text, busy_hud(text, club), glm::mat4(1.0f));
            CHECK(batch.truncated_text_count == 0U);
        }

        // Every refusal the server can send, and waiting for the group, as the notice.
        std::vector<std::string> notices = online_messages(text);
        notices.push_back(text_scorecard_waiting_for_group);
        for (const std::string& message : notices) {
            render_data data = busy_hud(text, longest_named_club());
            data.notice_label = lookup_text(text, message.c_str());
            overlay_batch batch;
            batch.grid = grid;
            draw_hud(batch, text, data, glm::mat4(1.0f));
            CHECK(batch.truncated_text_count == 0U);
        }

        for (const course_definition& course : shipped_content().courses) {
            for (const int players : {1, shipped_content().tuning.server.group_capacity}) {
                scorecard_data scorecard = played_scorecard(course, text, players);
                scorecard.next_round = players > 1;
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

        // The datestamp at its widest in every month under OFFLINE, with the HUD; the VCR's tags.
        for (const vcr_tag tag : {vcr_tag::play, vcr_tag::pause}) {
            for (int month = 1; month <= 12; ++month) {
                render_vcr vcr;
                vcr.tag = tag;
                vcr.show_datestamp = true;
                vcr.show_offline = true;
                const double day = static_cast<double>(days_from_civil(1989, month, 28)) + (12.0 + 59.0 / 60.0) / 24.0;
                vcr.datestamp = make_datestamp(text, day, 0.0);
                overlay_batch batch;
                batch.grid = grid;
                draw_hud(batch, text, busy_hud(text, longest_named_club()), glm::mat4(1.0f));
                draw_datestamp(batch, text, vcr);
                draw_vcr_tag(batch, text, vcr);
                CHECK(batch.truncated_text_count == 0U);
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
