#include "doctest.h"
#include "test_support.h"
#include "core/tee_book.h"
#include "core/vcr_frame.h"
#include "game/text_ids.h"
#include "renderer/hud_overlay.h"
#include "game/hub_flow.h"
#include "game/tee_times.h"
#include "game/hub_scene.h"
#include "game/interactables.h"
#include "game/online_play.h"
#include "game/group_round.h"
#include "game/golf_bag.h"
#include "game/address_play.h"
#include "renderer/prop_batch.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/hole_sign_face.h"
#include "renderer/shadow_casters.h"
#include "renderer/scorecard_overlay.h"
#include "game/scorecard.h"
#include "game/sight_line.h"
#include "renderer/camera_local.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr std::int64_t minute = 60000000;
game_state hub_game() {
    auto game = started_game(fixture_hub_course());
    game.play = play_mode::online; game.place.id = online_hub_course;
    game.kind = place_kind::hub; game.hub = hub_definition{};
    game.world.collectibles.clear();
    for (auto& start : game.world.hole_starts) { start.position.x += 100; }
    game.bag.where = bag_where::on_ground; game.bag.position = glm::vec3(100,0,100);
    game.world.hub = hub_anchors{{glm::vec3(0,0,16),2}};
    game.world.bus_stop = bus_stop{glm::vec3(6,0,16), 0.0f};
    place_hub_scene(game.area, game.world, test_props(), game.tuning);
    game.online.account_id = 1; game.online.room_ready = true;
    game.online.room = online_room{7, online_hub_course, 5, 1};
    game.online.server_now = 100 * minute;
    game.online.players[1].motion = current_motion(game);
    return game;
}
startup_catalog courses() {
    startup_catalog catalog;
    course_definition course; course.id = "course"; course.name = "Meadow Course"; course.carts = true;
    catalog.courses.push_back({course,0,std::nullopt});
    return catalog;
}
input_state accept() { input_state input; input.enter.pressed = true; return input; }
void tick(tee_book_state& menu, game_state& game, const input_state& input = {}) {
    update_tee_book(menu, game, courses(), input, {}, {});
}
void list_ready(tee_book_state& menu, game_state& game) {
    game.hub_flow.screen = hub_screen::book; tick(menu, game); tick(menu, game, accept());
    REQUIRE(menu.pending == net_command_type::open_tee_book);
    game.online.book_ready = true; game.online.book_course_id = "course";
    for (int i = 0; i < 96; ++i) {
        tee_slot_info slot; slot.course_id = "course"; slot.scheduled_at = (105 + i*15) * minute;
        slot.booked_count = 3; game.online.tee_slots[static_cast<std::uint64_t>(i+1)] = slot;
    }
    tick(menu, game);
}
// Screen position of a bus point from an eye behind it, looking along it.
glm::vec2 seen_from_behind(const static_prop& behind_of, const static_prop& bus, const glm::vec3& local) {
    const auto eye = prop_point(behind_of, glm::vec3(0, 1.7f, -15));
    const auto vp = glm::perspective(glm::radians(50.0f), 4.0f/3.0f, .1f, 100.0f) *
        glm::lookAt(eye, prop_point(behind_of, glm::vec3(0, 1.5f, 0)), world_up);
    const auto seen = project_to_screen(vp, prop_point(bus, local));
    CHECK(seen);
    return seen.value_or(glm::vec2(99));
}
}

TEST_CASE("hub F anchors match reachable models and physical collision, offline has no book") {
    auto game = hub_game(); game.player.position = glm::vec3(0,0,14.1f);
    auto interaction = nearest_interactable(game); REQUIRE(interaction); CHECK(interaction->kind == interactable_kind::tee_book);
    game.player.position.z = 13.9f;
    const auto found = interactables_in_reach(game);
    CHECK(std::none_of(found.begin(), found.end(), [](const auto& item){ return item.kind == interactable_kind::tee_book; }));
    REQUIRE(game.area.bus);
    game.player.position = game.area.bus->position;
    REQUIRE(nearest_interactable(game)); CHECK(nearest_interactable(game)->kind == interactable_kind::bus);
    game.play = play_mode::offline;
    REQUIRE(nearest_interactable(game)); CHECK(nearest_interactable(game)->kind == interactable_kind::bus);
    play_area area = game.area; area.props.clear();
    const auto count = area.solids.size(); place_hub_scene(area, game.world, test_props(), game.tuning);
    REQUIRE(area.props.size() == 2); CHECK(area.solids.size() == count+2);
    prop_batch cached, shadows;
    append_world_props(cached, area.props, test_props(), test_scene_colors().glow);
    world_marker_batch figures; render_data frame;
    build_shadow_casters(figures, shadows, frame, test_scene_colors(), test_props(), test_shadow_map(0.2f), &cached);
    REQUIRE(shadows.opaque().size() == cached.opaque().size());
    CHECK(shadows.opaque().front().position == cached.opaque().front().position);
    CHECK(shadows.opaque().back().position == cached.opaque().back().position);
    const auto& book = test_props().at("tee_book");
    CHECK(near(area.props[0].position + book.point("approach"), anchor_on_terrain(game.area, game.world.hub->book.position)));
    const auto& bus = test_props().at("hub_bus");
    CHECK(horizontal_distance(prop_point(area.props[1], bus.point("door")), area.bus->position) < area.bus->reach);
    for (std::size_t i=0; i<2; ++i) {
        const auto& body=area.solids[count+i]; CHECK(body.stops_ball); CHECK(body.stops_walker); CHECK(body.stops_cart);
        CHECK(body.base.y + body.height > area.props[i].position.y + 1.0f);
    }
    for(std::uint64_t i=1;i<=5;++i){ game.online.players[i].name="GOLFER "+std::to_string(i); game.online.players[i].group_id=1; game.online.players[i].round_strokes={3}; }
    const auto card=build_scorecard_data(game,shipped_text_assets().strings);
    for (const overlay_grid grid : {overlay_grid{640,480},overlay_grid{854,480},overlay_grid{1120,480}}) {
        overlay_batch batch; batch.grid=grid; draw_course_results(batch,shipped_text_assets(),card); CHECK(batch.truncated_text_count==0);
    }
}

TEST_CASE("parked bus: upright, door on its right from behind and toward the approach, at any heading") {
    const auto& model = test_props().at("hub_bus");
    const auto door = model.point("door"), across = model.point("door") * glm::vec3(-1, 1, 1);
    for (const float heading : {0.0f, 1.0f, 1.5708f, 3.1416f, -2.2f}) {
        auto game = hub_game();
        game.world.bus_stop->heading = heading;
        play_area area = game.area; area.props.clear(); area.bus.reset();
        place_hub_scene(area, game.world, test_props(), game.tuning);
        const static_prop placed = area.props.back();
        REQUIRE(placed.model == "hub_bus");
        // Denmark drives on the right: from behind, the kerb door is right of the far side.
        CHECK(seen_from_behind(placed, placed, door).x > seen_from_behind(placed, placed, across).x);
        CHECK(seen_from_behind(placed, placed, glm::vec3(0, 3, 0)).y > seen_from_behind(placed, placed, glm::vec3(0)).y);
        // A wrong heading sign turns the bus round, so its door shows on the left.
        if (std::abs(std::sin(heading)) > .75f) {
            static_prop mirrored = placed; mirrored.heading = -heading;
            CHECK(seen_from_behind(placed, mirrored, door).x < seen_from_behind(placed, mirrored, across).x);
        }
        const auto centre = prop_point(placed, glm::vec3(0, 1.5f, 0));
        REQUIRE(area.bus);
        CHECK(glm::dot(horizontal(prop_point(placed, door) - centre), horizontal(area.bus->position - centre)) > 0);
        // Facing the bus from a step behind the approach, its near side is the door, on screen.
        const auto at_door = prop_point(placed, door);
        const auto eye = area.bus->position + safe_normalize(horizontal(area.bus->position - at_door), glm::vec3(1, 0, 0)) * 3.0f +
            glm::vec3(0, 1.7f, 0);
        const auto vp = glm::perspective(glm::radians(50.0f), 4.0f/3.0f, .1f, 100.0f) *
            glm::lookAt(eye, glm::vec3(at_door.x, eye.y, at_door.z), world_up);
        const auto seen = project_to_screen(vp, at_door);
        REQUIRE(seen); CHECK(std::abs(seen->x) < 1); CHECK(std::abs(seen->y) < 1);
        CHECK(glm::distance(eye, at_door) < glm::distance(eye, centre));
    }
}

TEST_CASE("static model boxes block sight at their visible body without an imaginary wall to the origin") {
    auto game = hub_game();
    play_area area = game.area; area.props.clear();
    place_hub_scene(area, game.world, test_props(), game.tuning);
    const auto& body = area.solids.back();
    sight_scene scene; scene.area = &area;
    const auto eye = body.base + glm::vec3(0,1.5f,-10);
    REQUIRE(standing_hit(scene,eye,glm::vec3(0,0,1),20));
    const auto clear_eye = body.base + glm::vec3(5,1.5f,-10);
    const auto hit = standing_hit(scene,clear_eye,glm::vec3(0,0,1),20);
    CHECK(!hit || *hit > 14);
}

TEST_CASE("offline tee book chooses PLAY NOW and both buses preserve solo progress") {
    auto game = make_game_state(fixture_content(), save_data{});
    REQUIRE(enter_place(game, online_hub_course));
    startup_catalog catalog;
    catalog.courses.push_back({fixture_hub_course(), 0, std::nullopt});
    tee_book_state menu;
    game.hub_flow.screen = hub_screen::book;
    const auto progress = save_data_to_json(game.save);
    update_tee_book(menu, game, catalog, {}, {}, {});
    const auto page = render_tee_book(menu, game, catalog, shipped_text_assets());
    REQUIRE(page.tiles.size() == 2);
    CHECK(page.tiles.front().subtitle == "PLAY NOW");
    update_tee_book(menu, game, catalog, accept(), {}, {});
    REQUIRE(active_booking(game));
    CHECK(active_booking(game)->course_id == fixture_hub_course().id);
    CHECK(game.net_commands.empty()); CHECK(!game.online.reservation);
    update_tee_book(menu, game, catalog, {}, {}, {});
    CHECK(!menu.pending);
    game.hub_flow.screen = hub_screen::bus;
    update_tee_book(menu, game, catalog, {}, {}, {});
    update_tee_book(menu, game, catalog, accept(), {}, {});
    CHECK(game.hub_flow.travel_course == fixture_hub_course().id);
    REQUIRE(enter_place(game, game.hub_flow.travel_course));
    CHECK(course_warmup(game)); CHECK(!game.ball);
    request_ball_preparation(game, net_command_type::tee_up, 0);
    REQUIRE(game.ball); CHECK(game.ball->kind == ball_kind::warmup);
    CHECK(game.clubs[game.selected_club].club_class == "putter");
    // Any club can be picked before the round; the warmup ball is still putted.
    open_club_picker(game); CHECK(game.picker); game.picker.reset();
    game.selected_club = 0; REQUIRE(game.clubs[0].club_class != "putter");
    step_up_to_ball(game);
    CHECK(game.mode == game_mode::addressing); CHECK(game.clubs[game.selected_club].club_class == "putter");
    game.mode = game_mode::walking; game.address = address_state{};
    game.ball->strokes = 2; complete_current_hole(game);
    CHECK(!hole_played(*game.round, 0));
    request_ball_preparation(game, net_command_type::start_booked_round);
    REQUIRE(game.ball); CHECK(game.ball->kind == ball_kind::scored);
    CHECK(!course_warmup(game));
    request_bus_travel(game, false);
    REQUIRE(enter_place(game, game.hub_flow.travel_course));
    CHECK(game.kind == place_kind::hub); CHECK(!game.round); CHECK(!game.ball);
    CHECK(save_data_to_json(game.save) == progress);
    CHECK(game.net_commands.empty());
}

TEST_CASE("a booking still in the hub is missed at its time; one on its course has until expiry") {
    auto game = hub_game();
    const std::int64_t at = 100 * 15 * minute_micros;
    game.online.reservation = tee_reservation_info{}; game.online.reservation->scheduled_at = at;
    game.online.server_now = at - 1;
    CHECK(!reservation_expired(game));
    game.online.server_now = at;
    CHECK(reservation_expired(game));
    game.kind = place_kind::course;
    CHECK(!reservation_expired(game));
    game.online.server_now = at + game.tuning.server.tee_expiry_minutes * minute_micros;
    CHECK(reservation_expired(game));
    game.online.reservation->started_at = at;
    game.kind = place_kind::hub;
    CHECK(!reservation_expired(game));
}

TEST_CASE("bus and tee book boards: labelled by booking and place, read left to right from the front") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    const auto index = [&](const char* model) {
        const auto found = std::find_if(game.area.props.begin(), game.area.props.end(),
            [&](const static_prop& prop) { return prop.model == model; });
        CHECK(found != game.area.props.end());
        return static_cast<std::size_t>(found - game.area.props.begin());
    };
    const std::size_t book = index("tee_book"), bus = index("hub_bus");
    auto labels = prop_board_labels(game, courses(), text);
    REQUIRE(labels.size() == game.area.props.size());
    CHECK(labels[book] == lookup_text(text, text_hub_book_title));
    CHECK(labels[bus] == lookup_text(text, text_hub_bus_title));
    game.online.reservation = tee_reservation_info{}; game.online.reservation->course_id = "course";
    labels = prop_board_labels(game, courses(), text);
    CHECK(labels[bus] == "Meadow Course");

    render_hole_signs signs;
    const auto& sign_model = test_props().at("hole_sign");
    auto boards = prop_boards(game.area, test_props(), labels);
    REQUIRE(boards.size() == 2);
    CHECK(refresh_render_hole_signs(signs, game.area, {}, boards, text, test_scene_colors(), sign_model, 1));
    CHECK(!refresh_render_hole_signs(signs, game.area, {}, boards, text, test_scene_colors(), sign_model, 2));
    game.kind = place_kind::course;
    labels = prop_board_labels(game, courses(), text);
    CHECK(labels[bus] == lookup_text(text, text_hub_bus_board_hub));
    boards = prop_boards(game.area, test_props(), labels);
    CHECK(refresh_render_hole_signs(signs, game.area, {}, boards, text, test_scene_colors(), sign_model, 3));
    CHECK(signs.truncated_text_count == 0);
    // No hole sign texts were given: the way signs come first, then the boards.
    REQUIRE(signs.signs.size() == game.area.way_signs.size() + 2);
    const std::size_t first_board = signs.signs.size() - 2;
    // The book's title faces its reader at the approach.
    CHECK(glm::dot(boards[0].normal, horizontal(game.world.hub->book.position - boards[0].center)) > 0);
    for (std::size_t i = 0; i < boards.size(); ++i) {
        const auto eye = boards[i].center + boards[i].normal * 4.0f;
        const auto vp = glm::perspective(glm::radians(50.0f), 4.0f/3.0f, .1f, 100.0f) * glm::lookAt(eye, boards[i].center, world_up);
        std::optional<glm::vec2> start, end, top;
        const auto& drawn = signs.signs[first_board + i];
        for (std::size_t v = drawn.first_vertex; v < drawn.first_vertex + drawn.vertex_count; ++v) {
            const auto& vertex = signs.vertices[v];
            if (vertex.uv == glm::vec2(0, 0)) { start = project_to_screen(vp, vertex.position); }
            if (vertex.uv == glm::vec2(1, 0)) { end = project_to_screen(vp, vertex.position); }
            if (vertex.uv == glm::vec2(0, 1)) { top = project_to_screen(vp, vertex.position); }
        }
        REQUIRE(start); REQUIRE(end); REQUIRE(top);
        CHECK(start->x < end->x); CHECK(top->y > start->y);
    }
    // Every shipped course name fits the destination board.
    for (const auto& course : shipped_content().courses) {
        overlay_batch batch;
        draw_prop_board_face(batch, text, course.name, static_cast<int>(std::lround(prop_board_face_height *
            boards[1].half_size.x / boards[1].half_size.y)));
        CHECK(batch.truncated_text_count == 0);
    }
}

TEST_CASE("tee times read the hub's in-game clock: 15 real minutes apart, never a real date or UTC") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    game.tuning.sky_clock = sky_timeline{0, 0.0, 6.0, 1989};
    game.hub_sky.utc_offset_hours = 2.0f;
    // Real 12:00 UTC is in-game 72 hours on: midnight UTC, 02:00 at the hub.
    game.online.server_now = 12LL * 60 * minute;
    CHECK(tee_time_text(game.online.server_now, game, text) == make_datestamp(text, 3.0, 2.0).time);
    const auto next = tee_time_text(game.online.server_now + 15 * minute, game, text);
    CHECK(next == make_datestamp(text, 3.0 + 1.5 / 24.0, 2.0).time);
    CHECK(next.find("UTC") == std::string::npos);
    // Four real hours on is a later in-game day.
    const auto later = tee_time_text(game.online.server_now + 240 * minute, game, text);
    CHECK(later == format_text(text, text_hub_time_later_day, {{"time", make_datestamp(text, 4.0, 2.0).time}, {"days", "1"}}));
}
