#include "doctest.h"
#include "test_support.h"
#include "core/scene_signs.h"
#include "core/departure_board.h"
#include "core/tee_book.h"
#include "game/picture_loader.h"
#include "renderer/menu_overlay.h"
#include "renderer/picture_size.h"
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
    game.kind = place_kind::hub; game.hub = hub_definition{}; game.place.rules = place_rules{false, true, false, true, 10};
    game.world.collectibles.clear();
    for (auto& start : game.world.hole_starts) { start.position.x += 100; }
    game.bag.where = bag_where::on_ground; game.bag.position = glm::vec3(100,0,100);
    game.world.hub = hub_anchors{{glm::vec3(0,0,16),2}, {}, {}};
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
    // The cart or carry choice, then the course.
    REQUIRE(page.tiles.size() == 2);
    // Esc and R do the rest: no back or refresh rows, no key footer.
    CHECK(page.footer.empty());
    CHECK(page.tiles.front().title == "TRANSPORT: TAKE A CART");
    // The course's card: its time of day there, then its season.
    CHECK(page.screen == startup_menu_screen::course_book);
    CHECK(page.tiles.back().subtitle.find(" - ") != std::string::npos);
    menu.selection = 1;
    update_tee_book(menu, game, catalog, accept(), {}, {});
    REQUIRE(active_booking(game));
    CHECK(active_booking(game)->course_id == fixture_hub_course().id);
    CHECK(active_booking(game)->takes_cart);
    CHECK(game.net_commands.empty()); CHECK(!game.online.reservation);
    update_tee_book(menu, game, catalog, {}, {}, {});
    CHECK(!menu.pending);
    // F at the bus boards: no menu, and the booking's choice goes with me.
    request_bus_travel(game);
    CHECK(game.hub_flow.screen == hub_screen::none);
    CHECK(game.takes_cart);
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
    // On a course the bus offers the way back.
    game.hub_flow.screen = hub_screen::bus;
    update_tee_book(menu, game, catalog, {}, {}, {});
    update_tee_book(menu, game, catalog, accept(), {}, {});
    CHECK(game.hub_flow.travel_course == online_hub_course);
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
    auto labels = prop_board_labels(game, courses(), text, "");
    REQUIRE(labels.size() == game.area.props.size());
    CHECK(labels[book] == lookup_text(text, text_hub_book_title));
    CHECK(labels[bus] == lookup_text(text, text_hub_bus_title));
    game.online.reservation = tee_reservation_info{}; game.online.reservation->course_id = "course";
    labels = prop_board_labels(game, courses(), text, "");
    CHECK(labels[bus] == "Meadow Course");

    render_hole_signs signs;
    const auto& sign_model = test_props().at("hole_sign");
    auto boards = prop_boards(game.area, test_props(), labels);
    REQUIRE(boards.size() == 2);
    CHECK(refresh_render_hole_signs(signs, game.area, 1, {}, boards, text, test_scene_colors(), sign_model, 1));
    CHECK(!refresh_render_hole_signs(signs, game.area, 1, {}, boards, text, test_scene_colors(), sign_model, 2));
    game.kind = place_kind::course;
    labels = prop_board_labels(game, courses(), text, "");
    CHECK(labels[bus] == lookup_text(text, text_hub_bus_board_hub));
    boards = prop_boards(game.area, test_props(), labels);
    CHECK(refresh_render_hole_signs(signs, game.area, 1, {}, boards, text, test_scene_colors(), sign_model, 3));
    CHECK(signs.truncated_text_count == 0);

    // The scene updates the bus board by itself, and only when its text changes.
    render_hole_signs scene_signs;
    game.kind = place_kind::hub;
    game.online.reservation.reset();
    CHECK(refresh_scene_hole_signs(scene_signs, game, courses(), text, test_scene_colors(), test_props(), ""));
    CHECK(!refresh_scene_hole_signs(scene_signs, game, courses(), text, test_scene_colors(), test_props(), ""));
    const rgba_image unbooked_face = scene_signs.signs.back().face;
    game.online.reservation = tee_reservation_info{};
    game.online.reservation->course_id = "course";
    CHECK(refresh_scene_hole_signs(scene_signs, game, courses(), text, test_scene_colors(), test_props(), ""));
    CHECK(scene_signs.revision == 2U);
    CHECK(scene_signs.signs.back().face.pixels != unbooked_face.pixels);
    game.kind = place_kind::course;
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
    // Four real hours on is the next in-game day: TOMORROW, never a "+1D".
    const auto later = tee_time_text(game.online.server_now + 240 * minute, game, text);
    CHECK(later == "TOMORROW " + make_datestamp(text, 4.0, 2.0).time);
}

namespace {
// A catalog course with a hole count, par and length, dated `month`/`day`.
startup_course_option card_course(const char* id, const char* name, const bool carts, const int month, const int day) {
    startup_course_option option;
    option.course.id = id; option.course.name = name; option.course.carts = carts;
    option.course.holes = {"1.json", "2.json", "3.json"};
    option.course.sky.month = month; option.course.sky.day = day;
    option.total_par = 11; option.length_meters = 5912;
    return option;
}
void set_hub_clock(game_state& game, const std::int64_t now) {
    game.tuning.sky_clock = sky_timeline{0, 0.0, 6.0, 1989};
    game.hub_sky.utc_offset_hours = 2.0f;
    game.online.server_now = now;
}
}

TEST_CASE("the tee book's courses are cards: name, holes, par and length, then the time there, season and carts") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    startup_catalog catalog;
    catalog.courses = {card_course("links", "Links Course", false, 6, 21), card_course("park", "Park Course", true, 3, 19)};
    tee_book_state menu;
    game.hub_flow.screen = hub_screen::book;
    update_tee_book(menu, game, catalog, {}, {}, {});
    const auto page = render_tee_book(menu, game, catalog, text);
    CHECK(page.screen == startup_menu_screen::course_book);
    REQUIRE(page.tiles.size() == 2);
    CHECK(page.tiles[0].title == "Links Course");
    CHECK(page.tiles[0].figures == "3 HOLES  PAR 11  5,912 M");
    // Midsummer begins on the 21st of June; a course without carts says so.
    CHECK(page.tiles[0].subtitle.find(" - MIDSUMMER - CARRY ONLY") != std::string::npos);
    // The 19th of March is still winter, and a cart is no remark.
    CHECK(page.tiles[1].subtitle.find(" - WINTER") != std::string::npos);
    CHECK(page.tiles[1].subtitle.find("CARRY") == std::string::npos);
    CHECK(with_thousands(0) == "0");
    CHECK(with_thousands(999) == "999");
    CHECK(with_thousands(1000) == "1,000");
    CHECK(with_thousands(1234567) == "1,234,567");
}

TEST_CASE("every shipped course's card fits its text at every aspect, and an overlong name is cut off, not overflowed") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    startup_catalog catalog = *load_startup_catalog(shipped_content(), text).catalog;
    REQUIRE(catalog.courses.size() >= 4);
    const int lines = load_picture_style(asset_root())->target_lines;
    game.hub_flow.screen = hub_screen::book;
    for (const bool long_name : {false, true}) {
        if (long_name) { catalog.courses[0].course.name = std::string(80, 'W'); }
        tee_book_state menu;
        for (int selection = 0; selection < static_cast<int>(catalog.courses.size()); ++selection) {
            menu.selection = selection;
            update_tee_book(menu, game, catalog, {}, {}, {});
            const auto page = render_tee_book(menu, game, catalog, text);
            bool shows_long_name = false;
            for (const auto& tile : page.tiles) { shows_long_name = shows_long_name || tile.title == std::string(80, 'W'); }
            for (const auto& [width, height] : std::vector<std::pair<int, int>>{{1440, 1080}, {1920, 1080}, {2560, 1080}}) {
                const picture_size size = picture_size_for_window(width, height, lines);
                overlay_batch batch;
                batch.grid = overlay_grid{size.width, size.height};
                draw_startup_menu(batch, text, page, test_scene_colors().ground);
                // Only an overlong name is ever cut, and it is.
                CHECK((batch.truncated_text_count > 0U) == shows_long_name);
            }
        }
    }
}

TEST_CASE("the book lists today's times, then tomorrow's under their headings, across the hub's midnight") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    // The hub's days begin at 03:40, 07:40, 11:40, 15:40, 19:40 and 23:40 UTC. 15:50 is early in a day.
    set_hub_clock(game, (15 * 60 + 50) * minute);
    tee_book_state menu; list_ready(menu, game);
    const auto rows = tee_times(game.online.server_now, game.tuning.server, hub_clock{game.tuning.sky_clock, 2.0});
    REQUIRE(rows.size() == 31);  // 16:00 to 23:30
    const auto at = [](const int hours, const int minutes) { return (hours * 60 + minutes) * minute; };
    // Courses, transport, TODAY, 16:00..19:30 (15 times), TOMORROW, 19:45 on.
    auto page = render_tee_book(menu, game, courses(), text);
    REQUIRE(page.tiles.size() == 9);
    CHECK(page.screen == startup_menu_screen::tee_book);
    CHECK(page.tiles[2].heading); CHECK(page.tiles[2].title == "TODAY");
    CHECK(page.tiles[3].title == tee_clock_text(at(16, 0), game, text));
    menu.top = 5;  // rows 15 to 23
    page = render_tee_book(menu, game, courses(), text);
    REQUIRE(page.tiles.size() == 9);
    CHECK(page.tiles[2].title == tee_clock_text(at(19, 30), game, text));
    CHECK(page.tiles[3].heading); CHECK(page.tiles[3].title == "TOMORROW");
    CHECK(page.tiles[4].title == tee_clock_text(at(19, 45), game, text));
    // Rows show the time alone, never a day suffix.
    for (const auto& tile : page.tiles) {
        CHECK(tile.title.find('+') == std::string::npos);
        if (!tile.heading) { CHECK(tile.title.find("TOMORROW") == std::string::npos); }
    }
    // Arrows and Enter pass over a heading: up from the time after it lands on the one before.
    menu.selection = 19;
    input_state up; up.up.pressed = true;
    tick(menu, game, up);
    CHECK(menu.selection == 16);
    input_state left; left.left.pressed = true;
    menu.selection = 19; tick(menu, game, left);
    CHECK(menu.selection == 17);
}

TEST_CASE("places that show a tee time say TOMORROW only for the next in-game day, never a plus or a date") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    set_hub_clock(game, (15 * 60 + 50) * minute);
    const std::int64_t today = (19 * 60 + 30) * minute;
    const std::int64_t tomorrow = (19 * 60 + 45) * minute;
    CHECK(tee_time_text(today, game, text).find("TOMORROW") == std::string::npos);
    CHECK(tee_time_text(tomorrow, game, text) == "TOMORROW " + tee_clock_text(tomorrow, game, text));
    startup_catalog catalog = courses();
    const std::int64_t expiry = game.tuning.server.tee_expiry_minutes * minute;
    for (const std::int64_t at : {(17 * 60) * minute, today, tomorrow}) {
        tee_reservation_info row; row.reservation_id = 1; row.course_id = "course"; row.scheduled_at = at;
        game.online.reservation = row;
        const std::string booking = booking_text(game, catalog, text);
        CHECK(booking.find('+') == std::string::npos);
        // The time and the first-strike expiry each say TOMORROW only when they are.
        CHECK((booking.find("TOMORROW " + tee_clock_text(at, game, text)) != std::string::npos) == (at >= tomorrow));
        CHECK((booking.find("TOMORROW " + tee_clock_text(at + expiry, game, text)) != std::string::npos) == (at + expiry >= tomorrow));
    }
    departure_info departure; departure.scheduled_at = tomorrow; departure.course_id = "course";
    CHECK(departure_text(departure, game, catalog, text, 1000).find("TOMORROW") == 0);
    departure.scheduled_at = today;
    CHECK(departure_text(departure, game, catalog, text, 1000).find("TOMORROW") == std::string::npos);
}

TEST_CASE("each course's clock runs its own hours ahead, so the tee book shows which is at sunrise") {
    auto game = hub_game();
    const auto& text = shipped_text_assets();
    game.tuning.sky_clock = sky_timeline{0, 0.0, 6.0, 1989};
    game.world.latitude_deg = 56.0f;
    game.world.longitude_deg = 10.0f;
    course_definition course;
    course.sky.month = 6;
    course.sky.day = 21;
    course.sky.utc_offset_hours = 2.0f;
    // Real 12:00 UTC is in-game midnight UTC: 02:00 there, a summer night.
    const std::int64_t now_ms = 12LL * 60 * 60 * 1000;
    const std::string night = course_time_text(course, game, text, now_ms);
    CHECK(night.find(make_datestamp(text, 3.0, 2.0).time) == 0);
    CHECK(night.find(lookup_text(text, text_hub_part_night)) != std::string::npos);
    // Shifted 8 hours on, the same moment is 10:00 there: morning.
    course.sky.clock_shift_hours = 8.0f;
    const std::string morning = course_time_text(course, game, text, now_ms);
    CHECK(morning.find(make_datestamp(text, 3.0 + 8.0 / 24.0, 2.0).time) == 0);
    CHECK(morning.find(lookup_text(text, text_hub_part_morning)) != std::string::npos);
    // The sun low before its noon is a sunrise, after it a sunset.
    CHECK(std::string(part_of_day_key(3.0f, true, 7.0f)) == text_hub_part_sunrise);
    CHECK(std::string(part_of_day_key(3.0f, false, 7.0f)) == text_hub_part_sunset);
    CHECK(std::string(part_of_day_key(-3.0f, true, 9.0f)) == text_hub_part_dawn);
    CHECK(std::string(part_of_day_key(50.0f, false, 0.5f)) == text_hub_part_midday);
}

namespace {
const net_command* last_command(const game_state& game, const net_command_type type) {
    for (auto it = game.net_commands.rbegin(); it != game.net_commands.rend(); ++it) { if (it->type == type) { return &*it; } }
    return nullptr;
}
startup_catalog carts_and_carry() {
    startup_catalog catalog = courses();
    course_definition walkers; walkers.id = "walkers"; walkers.name = "Walkers Links"; walkers.carts = false;
    catalog.courses.push_back({walkers, 0, std::nullopt});
    return catalog;
}
}

TEST_CASE("the tee book offers cart or carry with the time, and changing it books the same time again") {
    auto game = hub_game(); tee_book_state menu; list_ready(menu, game);
    const auto& text = shipped_text_assets();
    auto page = render_tee_book(menu, game, courses(), text);
    // The way back, the choice, then the times.
    REQUIRE(page.tiles.size() > 3);
    CHECK(page.tiles[1].title == "TRANSPORT: TAKE A CART");
    CHECK(page.tiles[1].subtitle == "ENTER TO CHANGE");
    // A time books with the choice: the cart, as nothing was changed.
    menu.selection = 2; game.net_commands.clear();
    tick(menu, game, accept());
    REQUIRE(menu.pending == net_command_type::book_tee);
    const auto* booked = last_command(game, net_command_type::book_tee);
    REQUIRE(booked);
    CHECK(booked->takes_cart); CHECK(booked->scheduled_at == 105 * minute); CHECK(booked->course_id == "course");
    // The server's row arrives with the choice: the request is answered.
    tee_reservation_info row; row.reservation_id = 4; row.revision = 1; row.course_id = "course"; row.scheduled_at = 105 * minute; row.takes_cart = true;
    game.online.reservation = row;
    tick(menu, game);
    CHECK(!menu.pending);
    // With a booking the row's choice is shown; changing it sends the same time again, the other way.
    page = render_tee_book(menu, game, courses(), text);
    CHECK(page.tiles[2].title == "TRANSPORT: TAKE A CART");
    menu.selection = 2; game.net_commands.clear();
    tick(menu, game, accept());
    REQUIRE(menu.pending == net_command_type::book_tee);
    booked = last_command(game, net_command_type::book_tee);
    REQUIRE(booked);
    CHECK(!booked->takes_cart); CHECK(booked->scheduled_at == 105 * minute); CHECK(booked->reservation_id == 4);
    // Until the row says so, the book still shows the booking as it is.
    CHECK(render_tee_book(menu, game, courses(), text).tiles[2].title == "TRANSPORT: TAKE A CART");
    // A refusal keeps the whole row, and the book shows it.
    game.online.reservation->revision = 1;
    std::vector<reducer_failure> refusals{{"book_tee", "group_full"}};
    update_tee_book(menu, game, courses(), {}, {}, refusals);
    CHECK(!menu.pending); CHECK(game.online.reservation->takes_cart);
    CHECK(render_tee_book(menu, game, courses(), text).tiles[2].title == "TRANSPORT: TAKE A CART");
    // Sent again and answered: the row's new choice shows.
    tick(menu, game, accept());
    REQUIRE(menu.pending == net_command_type::book_tee);
    game.online.reservation->takes_cart = false; game.online.reservation->revision = 2;
    tick(menu, game);
    CHECK(!menu.pending);
    CHECK(render_tee_book(menu, game, courses(), text).tiles[2].title == "TRANSPORT: CARRY YOUR BAG");
}

TEST_CASE("before a booking the choice is only picked, and the next time booked takes it; a course without carts offers only carry") {
    auto game = hub_game(); tee_book_state menu; list_ready(menu, game);
    menu.selection = 1; game.net_commands.clear();
    tick(menu, game, accept());
    CHECK(!menu.pending); CHECK(game.net_commands.empty()); CHECK(!menu.takes_cart);
    CHECK(render_tee_book(menu, game, courses(), shipped_text_assets()).tiles[1].title == "TRANSPORT: CARRY YOUR BAG");
    menu.selection = 4; tick(menu, game, accept());  // past TODAY, to the second time
    const auto* booked = last_command(game, net_command_type::book_tee);
    REQUIRE(booked); CHECK(!booked->takes_cart); CHECK(booked->scheduled_at == 120 * minute);

    // A course without carts has no choice to offer, and books carry whatever was picked.
    auto walking = hub_game(); tee_book_state walkers_menu;
    const auto walkers = carts_and_carry();
    walking.hub_flow.screen = hub_screen::book;
    update_tee_book(walkers_menu, walking, walkers, {}, {}, {});
    walkers_menu.selection = 1;
    update_tee_book(walkers_menu, walking, walkers, accept(), {}, {});
    REQUIRE(walkers_menu.pending == net_command_type::open_tee_book);
    walking.online.book_ready = true; walking.online.book_course_id = "walkers";
    update_tee_book(walkers_menu, walking, walkers, {}, {}, {});
    const auto page = render_tee_book(walkers_menu, walking, walkers, shipped_text_assets());
    REQUIRE(page.tiles.size() > 2);
    for (const auto& tile : page.tiles) { CHECK(tile.title.rfind("TRANSPORT", 0) != 0); }
    walkers_menu.selection = 1; walking.net_commands.clear();
    update_tee_book(walkers_menu, walking, walkers, accept(), {}, {});
    booked = last_command(walking, net_command_type::book_tee);
    REQUIRE(booked); CHECK(!booked->takes_cart); CHECK(booked->course_id == "walkers");
}

TEST_CASE("offline PLAY NOW takes the same choice: cart or carry, carry on a course without carts") {
    auto game = make_game_state(fixture_content(), save_data{});
    REQUIRE(enter_place(game, online_hub_course));
    startup_catalog catalog;
    catalog.courses.push_back({fixture_hub_course(), 0, std::nullopt});
    const std::string id = fixture_hub_course().id;
    const auto& text = shipped_text_assets();
    tee_book_state menu;
    game.hub_flow.screen = hub_screen::book;
    update_tee_book(menu, game, catalog, {}, {}, {});
    auto page = render_tee_book(menu, game, catalog, text);
    REQUIRE(page.tiles.size() == 2);
    CHECK(page.tiles[0].title == "TRANSPORT: TAKE A CART");
    CHECK(page.tiles[1].subtitle.find("CARRY ONLY") == std::string::npos);
    // PLAY NOW with the cart.
    menu.selection = 1;
    update_tee_book(menu, game, catalog, accept(), {}, {});
    REQUIRE(active_booking(game)); CHECK(active_booking(game)->takes_cart);
    update_tee_book(menu, game, catalog, {}, {}, {});
    CHECK(!menu.pending);
    // With a booking the choice changes it: the same course booked again, as carry.
    menu.selection = 0;
    update_tee_book(menu, game, catalog, accept(), {}, {});
    REQUIRE(active_booking(game)); CHECK(active_booking(game)->course_id == id); CHECK(!active_booking(game)->takes_cart);
    update_tee_book(menu, game, catalog, {}, {}, {});
    CHECK(!menu.pending);
    page = render_tee_book(menu, game, catalog, text);
    CHECK(page.tiles[0].title == "TRANSPORT: CARRY YOUR BAG");
    // And the bus takes the booking's choice along.
    request_bus_travel(game);
    CHECK(!game.takes_cart); CHECK(game.hub_flow.travel_course == id);

    // Picking carry first books carry; a course without carts books carry whatever was picked.
    auto again = make_game_state(fixture_content(), save_data{});
    REQUIRE(enter_place(again, online_hub_course));
    tee_book_state picking;
    again.hub_flow.screen = hub_screen::book;
    update_tee_book(picking, again, catalog, {}, {}, {});
    picking.selection = 0;
    update_tee_book(picking, again, catalog, accept(), {}, {});
    CHECK(!picking.takes_cart); CHECK(!active_booking(again));
    picking.selection = 1;
    update_tee_book(picking, again, catalog, accept(), {}, {});
    REQUIRE(active_booking(again)); CHECK(!active_booking(again)->takes_cart);

    auto walking = make_game_state(fixture_content(), save_data{});
    REQUIRE(enter_place(walking, online_hub_course));
    startup_catalog walkers;
    auto no_carts = fixture_hub_course(); no_carts.carts = false;
    walkers.courses.push_back({no_carts, 0, std::nullopt});
    tee_book_state walkers_menu;
    walking.hub_flow.screen = hub_screen::book;
    update_tee_book(walkers_menu, walking, walkers, {}, {}, {});
    CHECK(render_tee_book(walkers_menu, walking, walkers, text).tiles[1].subtitle.find("CARRY ONLY") != std::string::npos);
    walkers_menu.selection = 1;
    update_tee_book(walkers_menu, walking, walkers, accept(), {}, {});
    REQUIRE(active_booking(walking)); CHECK(!active_booking(walking)->takes_cart);
}

TEST_CASE("F at the hub's bus boards it with no menu; without a booking it says so") {
    auto game = hub_game();
    REQUIRE(game.area.bus);
    game.player.position = game.area.bus->position;
    game_input press; press.interact = true;
    game.net_commands.clear();
    update_game(game, press, 0.016f);
    CHECK(game.hub_flow.screen == hub_screen::none);
    CHECK(game.hub_flow.travel_course.empty());
    REQUIRE(game.notice); CHECK(game.notice->text_key == text_online_error_booking_required);
    CHECK(last_command(game, net_command_type::take_bus) == nullptr);
    // Booked: F boards at once, taking the booking's carry or cart.
    tee_reservation_info row; row.reservation_id = 4; row.course_id = "course"; row.scheduled_at = 105 * minute; row.takes_cart = true;
    game.online.reservation = row;
    game.player.position = game.area.bus->position;
    update_game(game, press, 0.016f);
    CHECK(game.hub_flow.screen == hub_screen::none);
    CHECK(game.hub_flow.travel_course == "course");
    CHECK(game.takes_cart);
    const auto* bus = last_command(game, net_command_type::take_bus);
    REQUIRE(bus); CHECK(bus->reservation_id == 4);
    // No motion follows it: it would name the room I have left.
    CHECK(game.net_commands.back().type == net_command_type::take_bus);
    // On a course the bus offers the way back instead.
    auto course = hub_game();
    course.kind = place_kind::course; course.place.id = "course";
    course.player.position = course.area.bus->position;
    update_game(course, press, 0.016f);
    CHECK(course.hub_flow.screen == hub_screen::bus);
    CHECK(course.hub_flow.travel_course.empty());
}
