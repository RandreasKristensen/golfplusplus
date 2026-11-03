#include "doctest.h"

#include "core/startup_flow.h"
#include "core/visual_tour.h"
#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/visual_tour_loader.h"
#include "physics/vector_math.h"
#include "test_support.h"

#include <cmath>
#include <cstddef>
#include <string>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

namespace {
const visual_tour& shipped_tour() {
    static const visual_tour tour = *load_visual_tour(asset_root());
    return tour;
}

// Where `world` lands on a 16:9 picture seen from `view`: x and y in -1..1
// across the screen when in sight; w <= 0 behind the camera.
glm::vec4 on_screen(const camera_view& view, const glm::vec3& world) {
    const glm::mat4 projection = glm::perspective(glm::radians(70.0f), 16.0f / 9.0f, 0.1f, 5000.0f);
    const glm::vec4 clip = projection * glm::lookAt(view.position, view.target, glm::vec3(0.0f, 1.0f, 0.0f)) * glm::vec4(world, 1.0f);
    return clip.w > 0.0f ? glm::vec4(glm::vec3(clip) / clip.w, clip.w) : clip;
}
}

TEST_CASE("the shipped visual tour loads, every stop's title in the string table") {
    const visual_tour& tour = shipped_tour();
    REQUIRE(!tour.stops.empty());
    for (const tour_stop& stop : tour.stops) {
        CHECK(shipped_text_assets().strings.entries.count(stop.title) == 1U);
    }
}

TEST_CASE("a visual tour without stops, with an unknown weather or a bad date is refused") {
    const std::string flight =
        R"("flight": {"speed_meters_per_second": 8, "height_meters": 6, "tree_clearance_meters": 3,
                      "look_ahead_meters": 35, "look_height_meters": 1, "ease_meters": 24})";
    const auto tour = [&flight](const std::string& date, const std::string& stops) {
        return R"({"date": ")" + date + R"(", "stop_seconds": 20, "title_seconds": 4, "transition_seconds": 6, )" + flight +
            R"(, "stops": [)" + stops + "]}";
    };
    const std::string fog = R"({"hour": 22, "weather": "fog", "wetness": 0.3, "title": "tour.foggy_night"})";
    const std::optional<visual_tour> good = parse_visual_tour_from_text(tour("1989-10-14", fog));
    REQUIRE(good.has_value());
    CHECK(good->stops[0].weather == weather_kind::fog);
    CHECK(near(good->stops[0].wetness, 0.3f));
    CHECK(!parse_visual_tour_from_text(tour("1989-10-14", "")));
    CHECK(!parse_visual_tour_from_text(tour("1989-13-14", fog)));
    CHECK(!parse_visual_tour_from_text(tour("1989-10-14", R"({"hour": 22, "weather": "smog", "wetness": 0, "title": "x"})")));
    CHECK(!parse_visual_tour_from_text(tour("1989-10-14", R"({"hour": 25, "weather": "fog", "wetness": 0, "title": "x"})")));
}

TEST_CASE("the tour flies over the ground and the trees, round the whole course") {
    const game_state game = started_game(fixture_hub_course());
    const tour_flight& flight = shipped_tour().flight;
    const tour_path path = make_tour_path(game, flight);
    REQUIRE(path.eye.size() > 10U);
    REQUIRE(path.eye.size() == path.ground.size());
    CHECK(near(path.length / static_cast<float>(path.eye.size()), tour_path_spacing, 0.5f * tour_path_spacing));
    for (std::size_t i = 0; i < path.eye.size(); ++i) {
        CHECK(path.eye[i].y >= path.ground[i].y + flight.height_meters - 0.01f);
    }
    REQUIRE(!game.area.trees.empty());
    for (const tree_instance& tree : game.area.trees) {
        const float top = sample_area(game.area, tree.position).point.y + tree.shape.trunk_height + tree.shape.leaf_height;
        for (const glm::vec3& eye : path.eye) {
            if (horizontal_distance(eye, tree.position) < tree.shape.leaf_radius) {
                CHECK(eye.y >= top + flight.tree_clearance_meters - 0.01f);
            }
        }
    }
    // The loop closes: its last point is a step from its first.
    CHECK(horizontal_distance(path.ground.back(), path.ground.front()) < 2.0f * tour_path_spacing);
}

TEST_CASE("the tour starts over the first tee looking down the first hole, and looks where it flies") {
    const game_state game = started_game(fixture_hub_course());
    const visual_tour& tour = shipped_tour();
    tour_state state = start_tour(game, tour, 0);
    const camera_view start = tour_camera(state, tour.flight);
    const glm::vec3 tee = game.holes[0].tee_position;
    const glm::vec3 pin = game.holes[0].pin_position;
    CHECK(horizontal_distance(start.position, tee) < tour_path_spacing);
    CHECK(start.position.y > tee.y);

    // The first hole's line of play lies ahead on screen, and the camera
    // looks down at the ground ahead, never up at the sky.
    const std::vector<glm::vec3> line = line_of_play(place_hole(game.course_holes[0], game.world.hole_starts[0]));
    const glm::vec3 ahead = point_along(line, tour.flight.look_ahead_meters);
    const glm::vec4 seen = on_screen(start, glm::vec3(ahead.x, sample_area(game.area, ahead).point.y, ahead.z));
    CHECK(seen.w > 0.0f);
    CHECK(std::abs(seen.x) < 0.2f);
    CHECK(seen.y < 0.0f);
    CHECK(start.target.y < start.position.y);
    CHECK(on_screen(start, pin).w > 0.0f);

    // A second on, the camera has moved the way it looks.
    state = advance_tour(state, tour, 1.0f, false);
    const camera_view later = tour_camera(state, tour.flight);
    const glm::vec3 moved = horizontal(later.position - start.position);
    CHECK(glm::length(moved) > 0.5f * tour.flight.speed_meters_per_second);
    CHECK(glm::dot(moved, horizontal(start.target - start.position)) > 0.0f);
}

TEST_CASE("the tour steps through its stops, holding each one's sky and weather, its title first") {
    const game_state game = started_game(fixture_hub_course());
    const visual_tour& tour = shipped_tour();
    tour_state state = start_tour(game, tour, tour.stops.size() - 1);
    CHECK(state.stop == tour.stops.size() - 1);

    const tour_stop& last = tour.stops.back();
    const sky_controls sky = tour_sky(state, tour);
    CHECK(sky.pinned_hour == last.hour);
    CHECK(sky.pinned_date == tour.date);
    const weather_state weather = tour_weather(state, tour, weather_state{}, game.tuning.weather);
    CHECK(weather.kind == last.weather);
    CHECK(weather.wetness == last.wetness);
    CHECK(tour_title(state, tour, shipped_text_assets()) == lookup_text(shipped_text_assets(), last.title.c_str()));

    // The title goes once it has shown; the stop runs to its end, then the tour wraps to the first.
    state = advance_tour(state, tour, tour.title_seconds + 0.1f, false);
    CHECK(tour_title(state, tour, shipped_text_assets()).empty());
    CHECK(state.stop == tour.stops.size() - 1);
    state = advance_tour(state, tour, tour.stop_seconds, false);
    CHECK(state.stop == 0U);
    CHECK(tour_weather(state, tour, weather_state{}, game.tuning.weather).kind == tour.stops[0].weather);

    // Enter moves on at once.
    state = advance_tour(state, tour, 0.0f, true);
    CHECK(state.stop == 1U);
    CHECK(state.stop_seconds == 0.0f);
}

TEST_CASE("between stops the hour runs on smoothly from the last one's, forwards through midnight") {
    const game_state game = started_game(fixture_hub_course());
    const visual_tour& tour = shipped_tour();
    REQUIRE(tour.transition_seconds > 0.0f);
    REQUIRE(tour.stops.back().hour > tour.stops.front().hour);
    tour_state state = start_tour(game, tour, tour.stops.size() - 1);
    state = advance_tour(state, tour, 0.0f, true);
    REQUIRE(state.stop == 0U);
    const float late = tour.stops.back().hour;
    const float early = tour.stops.front().hour;
    CHECK(near(*tour_sky(state, tour).pinned_hour, late));

    // Halfway, past midnight: later than the last stop or earlier than the first, never in between.
    state = advance_tour(state, tour, 0.5f * tour.transition_seconds, false);
    const float halfway = *tour_sky(state, tour).pinned_hour;
    CHECK((halfway > late || halfway < early));
    float previous = late;
    tour_state stepping = start_tour(game, tour, tour.stops.size() - 1);
    stepping = advance_tour(stepping, tour, 0.0f, true);
    for (int i = 0; i < 20; ++i) {
        stepping = advance_tour(stepping, tour, tour.transition_seconds / 20.0f, false);
        const float hour = *tour_sky(stepping, tour).pinned_hour;
        const float forwards = std::fmod(hour - previous + 24.0f, 24.0f);
        CHECK(forwards < 12.0f);
        previous = hour;
    }
    CHECK(near(*tour_sky(stepping, tour).pinned_hour, early, 1.0e-3f));
}

TEST_CASE("VISUAL TOUR picks a course and asks app to tour it; Escape goes back") {
    startup_catalog catalog;
    course_definition first;
    first.id = "first";
    course_definition second;
    second.id = "second";
    catalog.courses = {startup_course_option{first, 0, std::nullopt}, startup_course_option{second, 0, std::nullopt}};

    startup_flow_state state;
    state.selection = 2;
    input_state enter;
    enter.enter.pressed = true;
    update_startup_menu(state, enter, menu_pointer{}, catalog);
    REQUIRE(state.flow == startup_flow::tour_picker);
    CHECK(make_startup_menu_render_data(state, catalog, shipped_text_assets()).title == "VISUAL TOUR");

    state.selection = 1;
    const startup_menu_result result = update_startup_menu(state, enter, menu_pointer{}, catalog);
    CHECK(result.action == startup_action::start_tour);
    CHECK(result.course.id == "second");
    CHECK(state.flow == startup_flow::tour_picker);
    enter_touring(state);
    CHECK(state.flow == startup_flow::touring);

    input_state escape;
    escape.escape.pressed = true;
    return_to_main_menu(state);
    state.flow = startup_flow::tour_picker;
    update_startup_menu(state, escape, menu_pointer{}, catalog);
    CHECK(state.flow == startup_flow::main);
}
