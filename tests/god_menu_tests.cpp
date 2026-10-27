#include "doctest.h"

#include "core/god_menu.h"
#include "game/mode_dispatch.h"
#include "test_support.h"

#include <algorithm>
#include <optional>

namespace {
god_menu_keys pressed_right() {
    god_menu_keys keys;
    keys.right = true;
    return keys;
}

god_menu_keys pressed_down() {
    god_menu_keys keys;
    keys.down = true;
    return keys;
}

weather_state breezy() {
    weather_state weather;
    weather.wind = glm::vec3(0.0f, 0.0f, 4.0f);
    weather.wetness = 0.3f;
    return weather;
}
}

TEST_CASE("the god menu opens only when enabled, and its rows wrap") {
    god_menu_keys toggle;
    toggle.toggle = true;
    CHECK(!update_god_menu(god_menu_state{}, sky_controls{}, toggle, breezy(), 12.0f).menu.open);

    god_menu_state menu;
    menu.enabled = true;
    menu = update_god_menu(menu, sky_controls{}, toggle, breezy(), 12.0f).menu;
    REQUIRE(menu.open);
    god_menu_keys up;
    up.up = true;
    CHECK(update_god_menu(menu, sky_controls{}, up, breezy(), 12.0f).menu.row == god_menu_row::solids);
}

TEST_CASE("changing a live row starts from the live value, and backspace lets it run live again") {
    god_menu_state menu;
    menu.enabled = true;
    menu.open = true;
    god_menu_update next = update_god_menu(menu, sky_controls{}, pressed_right(), breezy(), 12.0f);
    REQUIRE(next.sky.pinned_hour.has_value());
    CHECK(*next.sky.pinned_hour == 12.5f);

    next = update_god_menu(next.menu, next.sky, pressed_down(), breezy(), 12.0f);
    next = update_god_menu(next.menu, next.sky, pressed_right(), breezy(), 12.0f);
    CHECK(next.menu.weather == std::optional<weather_kind>(weather_kind::cloudy));

    while (next.menu.row != god_menu_row::wind_speed) {
        next = update_god_menu(next.menu, next.sky, pressed_down(), breezy(), 12.0f);
    }
    next = update_god_menu(next.menu, next.sky, pressed_right(), breezy(), 12.0f);
    REQUIRE(next.menu.wind_speed.has_value());
    CHECK(near(*next.menu.wind_speed, 4.5f));

    god_menu_keys live;
    live.live = true;
    next = update_god_menu(next.menu, next.sky, live, breezy(), 12.0f);
    CHECK(!next.menu.wind_speed.has_value());
    CHECK(next.menu.weather.has_value());
}

TEST_CASE("the god menu holds the weather over the live one, only what it changed") {
    const weather_tuning& tuning = shipped_content().tuning.weather;
    god_menu_state menu;
    CHECK(!god_weather(menu, breezy(), tuning).has_value());

    menu.weather = weather_kind::heavy_rain;
    std::optional<weather_state> held = god_weather(menu, breezy(), tuning);
    REQUIRE(held.has_value());
    CHECK(held->rain == tuning.heavy_rain);
    CHECK(held->wind == breezy().wind);
    CHECK(held->wetness == breezy().wetness);

    menu.wind_speed = 10.0f;
    menu.wetness = 1.0f;
    held = god_weather(menu, breezy(), tuning);
    REQUIRE(held.has_value());
    CHECK(near(held->wind, glm::vec3(0.0f, 0.0f, 10.0f)));
    CHECK(held->wetness == 1.0f);
}

TEST_CASE("the god menu sets cloud, rain, fog and thunder each on its own, over a kind") {
    const weather_tuning& tuning = shipped_content().tuning.weather;
    god_menu_state menu;
    menu.enabled = true;
    menu.open = true;
    menu.weather = weather_kind::clear;
    menu.row = god_menu_row::rain;
    const weather_state clear = *god_weather(menu, breezy(), tuning);
    god_menu_update next = update_god_menu(menu, sky_controls{}, pressed_right(), clear, 12.0f);
    REQUIRE(next.menu.rain.has_value());
    CHECK(near(*next.menu.rain, 0.1f));

    // Rain on a clear sky only falls as a sun shower, and cannot thunder.
    menu.rain = tuning.heavy_rain;
    menu.thunder = 1.0f;
    const weather_state shower = *god_weather(menu, breezy(), tuning);
    CHECK(shower.kind == weather_kind::sun_shower);
    CHECK(shower.rain <= tuning.sun_shower_keep);
    CHECK(shower.rainbow > 0.0f);
    CHECK(shower.thunder == 0.0f);
    // Under cloud it pours and thunders, and clears any fog.
    menu.cloud = 1.0f;
    menu.fog = 0.5f;
    const weather_state storm = *god_weather(menu, breezy(), tuning);
    CHECK(storm.kind == weather_kind::thunderstorm);
    CHECK(storm.rain == tuning.heavy_rain);
    CHECK(storm.rainbow == 0.0f);
    CHECK(storm.fog == 0.0f);
    // Fog lies in still, dry air.
    menu.rain = 0.0f;
    menu.thunder = 0.0f;
    CHECK(god_weather(menu, breezy(), tuning)->fog > 0.4f);

    // Picking a kind lets its amounts run with it again.
    menu.row = god_menu_row::weather;
    next = update_god_menu(menu, sky_controls{}, pressed_right(), storm, 12.0f);
    CHECK(!next.menu.cloud.has_value());
    CHECK(!next.menu.rain.has_value());
    CHECK(!next.menu.fog.has_value());
    CHECK(!next.menu.thunder.has_value());
    CHECK(next.menu.weather == std::optional<weather_kind>(weather_kind::cloudy));
}

TEST_CASE("run_god_menu puts the held weather into play offline") {
    game_state state = started_hole();
    god_menu_state menu;
    menu.wetness = 0.8f;
    sky_controls sky;
    run_god_menu(menu, sky, state, god_menu_keys{});
    REQUIRE(state.weather_override.has_value());
    CHECK(course_weather_at(state, shot_unix_ms(state)).wetness == 0.8f);

    menu.wetness.reset();
    run_god_menu(menu, sky, state, god_menu_keys{});
    CHECK(!state.weather_override.has_value());
}

TEST_CASE("the god menu shows every solid near me as outlines, and hides them again") {
    god_menu_state menu;
    menu.enabled = true;
    menu.open = true;
    menu.row = god_menu_row::solids;
    menu = update_god_menu(menu, sky_controls{}, pressed_right(), breezy(), 12.0f).menu;
    CHECK(menu.show_solids);

    const game_state state = started_hole();
    const std::vector<world_segment> shown = god_solid_outlines(menu, state, state.player.position);
    CHECK(!shown.empty());
    for (const world_segment& edge : shown) {
        CHECK(horizontal_distance(edge.first, state.player.position) < 80.0f);
    }
    god_menu_keys live;
    live.live = true;
    menu = update_god_menu(menu, sky_controls{}, live, breezy(), 12.0f).menu;
    CHECK(!menu.show_solids);
    CHECK(god_solid_outlines(menu, state, state.player.position).empty());
}

TEST_CASE("a solid's outline: a box its twelve edges, an upright cylinder its two rings joined, a cone to its tip") {
    solid box;
    box.shape = solid_shape::box;
    box.base = glm::vec3(1.0f, 0.0f, 2.0f);
    box.along = glm::vec3(0.0f, 0.0f, 1.0f);
    box.half_length = 2.0f;
    box.half_width = 1.0f;
    box.height = 1.5f;
    const std::vector<world_segment> edges = solid_outline(box);
    CHECK(edges.size() == 12U);
    float top = 0.0f;
    for (const world_segment& edge : edges) {
        top = std::max({top, edge.first.y, edge.second.y});
        CHECK(std::abs(edge.first.z - box.base.z) <= 2.0001f);
        CHECK(std::abs(edge.first.x - box.base.x) <= 1.0001f);
    }
    CHECK(near(top, 1.5f));

    solid trunk;
    trunk.shape = solid_shape::cylinder;
    trunk.radius = 0.3f;
    trunk.height = 4.0f;
    for (const world_segment& edge : solid_outline(trunk)) {
        CHECK(near(horizontal_distance(edge.first, trunk.base), 0.3f, 0.0001f));
    }
    solid leaves = trunk;
    leaves.shape = solid_shape::cone;
    const std::vector<world_segment> cone = solid_outline(leaves);
    CHECK(near(cone.back().second, glm::vec3(0.0f, 4.0f, 0.0f)));
}
