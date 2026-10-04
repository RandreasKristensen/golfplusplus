#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/shot_simulation.h"
#include "server_content.h"

#include "server_test_support.h"

#include <string>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
bool same_shot(const shot_result& a, const shot_result& b) {
    if (a.rest_position != b.rest_position || a.holed != b.holed || a.duration != b.duration ||
        a.trajectory != b.trajectory || a.events.size() != b.events.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        if (a.events[i].time != b.events[i].time || a.events[i].kind != b.events[i].kind ||
            a.events[i].material != b.events[i].material) {
            return false;
        }
    }
    return true;
}

bool same_trees(const std::vector<tree_body>& a, const std::vector<tree_body>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].base != b[i].base || a[i].shape.trunk_radius != b[i].shape.trunk_radius ||
            a[i].shape.leaf_radius != b[i].shape.leaf_radius) {
            return false;
        }
    }
    return true;
}
}

TEST_CASE("the server loads the content it needs and only courses with a world") {
    const content_fixture fixture = fixture_content_files();
    const server_content_load_result loaded = parse_server_content(fixture.files);
    REQUIRE(loaded.content.has_value());
    CHECK(loaded.error.empty());
    CHECK(loaded.content->clubs.size() == shipped_content().clubs.size());
    CHECK(loaded.content->font.glyphs.size() > 26U);
    REQUIRE(loaded.content->courses.size() == 1U);
    CHECK(loaded.content->courses[0].id == fixture_hub_course().id);
    CHECK(find_server_course(*loaded.content, "missing") == nullptr);
}

TEST_CASE("missing server content is reported, not filled in") {
    content_fixture fixture = fixture_content_files();
    fixture.files.erase("tuning/game_tuning.json");
    const server_content_load_result loaded = parse_server_content(fixture.files);
    CHECK(!loaded.content.has_value());
    CHECK(loaded.error.find("tuning/game_tuning.json") != std::string::npos);

    fixture = fixture_content_files();
    const server_content content = *parse_server_content(fixture.files).content;
    fixture.files.erase("holes/test2.json");
    CHECK(!build_server_course(fixture.files, content, fixture_hub_course().id).has_value());
    CHECK(!build_server_course(fixture.files, content, "missing").has_value());
}

// Both sides run natively here, so this proves the server builds the same
// course from the same content. Native against WASM floating point is checked
// by tooling/net/check_determinism.ps1.
TEST_CASE("the server builds each hole where the game plays it, and shots land in the same place") {
    const content_fixture fixture = fixture_content_files();
    const server_content content = *parse_server_content(fixture.files).content;
    const std::optional<server_course> course = build_server_course(fixture.files, content, fixture_hub_course().id);
    REQUIRE(course.has_value());
    REQUIRE(course->shot_holes.size() == fixture_hub_course().holes.size());
    int tree_hits = 0;

    for (std::size_t i = 0; i < course->shot_holes.size(); ++i) {
        game_state game = started_game(fixture_hub_course());
        REQUIRE(start_hub_hole(game, i));
        const shot_course client = current_shot_course(game);
        const shot_course server{course->area, course->trees, course->shot_holes[i]};

        CHECK(server.hole.pin == client.hole.pin);
        CHECK(server.hole.wind_seed == client.hole.wind_seed);
        CHECK(same_trees(server.trees, client.trees));
        CHECK(course->holes[i].tee_position == game.hole->tee_position);

        shot_input input;
        input.ball_start = game.ball.position;
        input.aim_angle = game.aim_angle + glm::radians(-15.0f);
        input.club_id = "driver";
        input.power = 0.85f;
        input.wind_time = 3.0f;
        const shot_result on_client = simulate_shot(input, client, game.tuning, game.clubs, game.rewards);
        const shot_result on_server = simulate_shot(input, server, content.tuning, content.clubs, content.rewards);
        CHECK(on_client.duration > 0.0f);
        CHECK(same_shot(on_client, on_server));
        for (const shot_event& event : on_server.events) {
            tree_hits += event.kind == shot_event_kind::tree_hit ? 1 : 0;
        }
    }
    // The shots go through the trees, not only over open ground.
    CHECK(tree_hits > 0);
}
