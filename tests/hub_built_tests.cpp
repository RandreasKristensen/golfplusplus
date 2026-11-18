// What is built on the shipped hub's plaza and range, as the player meets it:
// where it stands on the screen from the arrival, what its boards say and what
// it stops.
#include "doctest.h"
#include "test_support.h"
#include "audio/audio_manifest.h"
#include "audio/sound_placement.h"
#include "core/departure_board.h"
#include "core/light_frame.h"
#include "core/tee_book.h"
#include "core/vcr_frame.h"
#include "game/area_solids.h"
#include "game/boarding.h"
#include "game/hub_scene.h"
#include "renderer/camera_local.h"
#include "game/tee_times.h"
#include "game/world_sounds.h"
#include "renderer/hole_sign_face.h"
#include "renderer/prop_batch.h"
#include "game/hub_session.h"
#include "game/model_kinds.h"
#include "game/text_ids.h"
#include "renderer/hud_overlay.h"
#include "physics/solid.h"
#include "game/shot_simulation.h"
#include <glm/gtc/matrix_transform.hpp>

namespace {
game_state shipped_hub() {
    game_state state = make_game_state(shipped_content(), save_data{});
    const bool started = start_hub(state, shipped_content().hub);
    CHECK(started);
    return state;
}

std::vector<const static_prop*> props_of(const game_state& state, const std::string& model) {
    std::vector<const static_prop*> found;
    for (const auto& prop : state.area.props) { if (prop.model == model) { found.push_back(&prop); } }
    return found;
}

// The view from the arrival: the player's eye looking along the arrival heading.
glm::mat4 arrival_view(const game_state& state, const float aspect) {
    const glm::vec3 eye = state.player.position + glm::vec3(0, 1.6f, 0);
    return glm::perspective(glm::radians(60.0f), aspect, .1f, 600.0f) *
        glm::lookAt(eye, eye + yaw_direction(state.player.yaw), world_up);
}
}

TEST_CASE("from the arrival facing north the clubhouse is to the left and the range ahead, at any aspect") {
    const auto state = shipped_hub();
    const auto clubhouse = props_of(state, "hub_clubhouse");
    REQUIRE(clubhouse.size() == 1);
    for (const float aspect : {4.0f / 3.0f, 16.0f / 9.0f, 21.0f / 9.0f}) {
        const auto view = arrival_view(state, aspect);
        // Its gable faces the putting green (east): its front is +x.
        CHECK(near(yaw_direction(clubhouse[0]->heading), glm::vec3(1, 0, 0), .001f));
        const glm::vec3 range_end(33.25f, 0.0f, 0.0f), range_middle(0.0f, 0.0f, 0.0f);
        const auto left = project_to_screen(view, clubhouse[0]->position);
        const auto east_end = project_to_screen(view, range_end);
        const auto middle = project_to_screen(view, range_middle);
        REQUIRE(left); REQUIRE(east_end); REQUIRE(middle);
        CHECK(left->x < middle->x);        // west of the range, so further left
        CHECK(middle->x < 0.0f);           // the range's middle is to the left too, the east end in front
        CHECK(std::abs(east_end->x) < .35f);
        CHECK(std::abs(east_end->y) < 1.0f);
        // The putting green (east) is to the right of the range.
        const auto green = project_to_screen(view, glm::vec3(60, 0, 48));
        REQUIRE(green);
        CHECK(green->x > east_end->x);
    }
    // Behind the player, nothing projects.
    CHECK(!project_to_screen(arrival_view(state, 16.0f / 9.0f), glm::vec3(state.player.position.x, 0, state.player.position.z + 30)));
}

TEST_CASE("the clocks write the hub's in-game time, the camcorder datestamp's, never the real time") {
    auto state = shipped_hub();
    const auto& text = shipped_text_assets();
    sky_controls sky; sky.pinned_hour = 14.5f;
    const std::string clock = place_clock_text(text, state, sky, 0);
    CHECK(clock.find("2:30") != std::string::npos);
    // The same whatever this machine's UTC says: it is the pinned hour.
    CHECK(place_clock_text(text, state, sky, 987654321000LL) == clock);
    const auto vcr = make_render_vcr(text, state, sky, 0, vcr_signals{false, false, true});
    CHECK(vcr.datestamp.time == clock);

    startup_catalog catalog;
    const auto labels = prop_board_labels(state, catalog, text, clock);
    REQUIRE(labels.size() == state.area.props.size());
    int clocks = 0;
    for (std::size_t i = 0; i < labels.size(); ++i) {
        if (state.area.props[i].model != clock_model) { continue; }
        ++clocks; CHECK(labels[i] == clock);
    }
    CHECK(clocks == 2);
    // One clock on the clubhouse gable, high on the wall facing the green; one over the starter hut's window.
    const auto clubhouse = props_of(state, "hub_clubhouse")[0];
    const auto hut = props_of(state, "hub_starter_hut")[0];
    bool on_gable = false, on_hut = false;
    for (const auto* face : props_of(state, clock_model)) {
        on_gable = on_gable || (horizontal_distance(face->position, clubhouse->position) < 7.0f && face->position.y > 4.0f);
        on_hut = on_hut || (horizontal_distance(face->position, hut->position) < 2.5f && face->position.y > 2.0f);
    }
    CHECK(on_gable); CHECK(on_hut);
}

TEST_CASE("each target's distance board says the metres from its tee line") {
    auto state = shipped_hub();
    const auto& text = shipped_text_assets();
    startup_catalog catalog;
    const auto labels = prop_board_labels(state, catalog, text, "");
    int seen = 0;
    for (const auto& target : state.hub->targets) {
        bool found = false;
        for (std::size_t i = 0; i < labels.size(); ++i) {
            const auto& prop = state.area.props[i];
            if (prop.model != distance_board_model || horizontal_distance(prop.position, target.board) > 1.0f) { continue; }
            found = true; ++seen;
            // The nearest bay of the line that fires at it (the hill tee for the targets east of the flat bays).
            float nearest = 1.0e9f;
            for (const auto& mat : state.hub->tee_mats) {
                if (!mat.raised) { continue; }
                const bool hill = mat.position.y > 10.0f;
                if (hill == (target.position.x > 40.0f)) { nearest = std::min(nearest, horizontal_distance(mat.position, target.position)); }
            }
            CHECK(labels[i] == std::to_string(std::lround(nearest)) + " M");
            // Faces the tee line (south).
            CHECK(near(yaw_direction(prop.heading), glm::vec3(0, 0, 1), .001f));
        }
        CHECK(found);
    }
    CHECK(seen == 12);
}

TEST_CASE("bay dividers stand between bays: a shank stops on one and a normal shot passes between") {
    auto state = shipped_hub();
    const auto dividers = props_of(state, "hub_range_divider");
    CHECK(dividers.size() == 25);
    std::vector<glm::vec3> bays;
    for (const auto& mat : state.hub->tee_mats) { if (mat.raised) { bays.push_back(mat.position); } }
    REQUIRE(bays.size() == 23);
    for (const auto& bay : bays) {
        int beside = 0;
        for (const auto* divider : dividers) {
            beside += std::abs(divider->position.x - bay.x) < 1.76f && std::abs(divider->position.x - bay.x) > 1.74f;
        }
        CHECK(beside == 2);
    }
    const auto fly = [&](const glm::vec3& bay, const glm::vec3& velocity) {
        ball_state ball;
        ball.radius = state.tuning.scale.ball_physics_radius_meters;
        ball.position = bay + glm::vec3(0, 0.3f, 0);
        ball.velocity = velocity;
        for (int i = 0; i < 400; ++i) {
            ball_state next = ball;
            next.position += velocity * shot_step_seconds;
            const auto met = resolve_ball_contacts(ball.position, next, state.area.solids, state.area.solids_grid, {}, 0);
            if (met.hit) { return std::make_pair(true, met.ball.position); }
            ball = met.ball;
        }
        return std::make_pair(false, ball.position);
    };
    const glm::vec3 bay = bays[10];
    for (const float side : {-1.0f, 1.0f}) {
        const auto [hit, where] = fly(bay, glm::vec3(side * 30.0f, 0, -3.0f));
        CHECK(hit);
        CHECK(std::abs(where.x - bay.x) < 1.76f);  // it never reached the next golfer's bay
    }
    // Straight down the bay, hooked a few degrees: through, and the divider stood no higher than a golfer's waist.
    CHECK(!fly(bay, glm::vec3(2.0f, 3.0f, -40.0f)).first);
    float top = 0.0f;
    for (const auto& body : state.area.solids) {
        if (body.kind == solid_kind::board && body.half_width < .1f && body.half_length > .9f) { top = std::max(top, solid_top(body) - body.base.y); }
    }
    CHECK(top > 0.5f); CHECK(top < 1.1f);
}

TEST_CASE("the putting green's flags are short and the targets' full height, in the drawn pins and the sticks that stop balls") {
    auto state = shipped_hub();
    const float full = state.tuning.scale.pin_visual_height_meters;
    const float short_flag = state.tuning.scale.practice_pin_visual_height_meters;
    CHECK(short_flag < full);
    for (std::size_t i = 0; i < state.area.cups.size(); ++i) { CHECK(near(pin_height_meters(state, i), short_flag, 1e-6f)); }
    CHECK(near(pin_height_meters(state, state.area.cups.size()), full, 1e-6f));  // the first target
    const auto sticks = flagstick_solids(state.area.cups, state.flags_in, state.tuning);
    for (const auto& stick : sticks) { if (stick.kind == solid_kind::flagstick) { CHECK(stick.height <= short_flag + state.area.cups[0].depth + .001f); } }
}

TEST_CASE("the hub's floodlights stand behind the tee line and light the range; its tee line is one levelled strip") {
    const auto state = shipped_hub();
    REQUIRE(state.area.floodlights.size() == 6);
    for (const auto& light : state.area.floodlights) {
        CHECK(light.foot.z > 4.0f);                     // behind the bays, never in a shot's way
        CHECK(light.aim.z < -0.1f);                     // aimed down the range
        CHECK(light.lamp.y > light.foot.y + 5.0f);
    }
    REQUIRE(state.area.tee_boxes.size() == 2);
    for (const auto& mat : state.hub->tee_mats) {
        if (!mat.raised) { continue; }
        int on = 0;
        for (const auto& box : state.area.tee_boxes) { on += on_tee_box(box, mat.position); }
        CHECK(on == 1);
    }
    CHECK(state.area.tee_boxes[0].half_width > 35.0f);
}

TEST_CASE("the car park's parked cars are solid: a ball hit along the row stops on the first, one down the aisle passes") {
    const auto state = shipped_hub();
    std::vector<const static_prop*> cars;
    for (const auto& prop : state.area.props) { if (prop.model.rfind("hub_car_", 0) == 0) { cars.push_back(&prop); } }
    REQUIRE(cars.size() >= 5);
    const auto fly_east = [&](const float z) {
        ball_state ball;
        ball.radius = state.tuning.scale.ball_physics_radius_meters;
        ball.position = glm::vec3(70.0f, 0.7f, z);
        const glm::vec3 velocity(20.0f, 0, 0);
        for (int i = 0; i < 600; ++i) {
            ball_state next = ball;
            next.position += velocity * shot_step_seconds;
            const auto met = resolve_ball_contacts(ball.position, next, state.area.solids, state.area.solids_grid, {}, 0);
            if (met.hit) { return std::make_pair(true, met.ball.position.x); }
            ball = met.ball;
        }
        return std::make_pair(false, ball.position.x);
    };
    // The north row's first car stands at x = 79: its bonnet or tail meets the ball a little short of its middle.
    const auto row = fly_east(61.7f);
    CHECK(row.first); CHECK(row.second > 77.0f); CHECK(row.second < 79.0f);
    // The aisle between the rows is open all the way across the lot.
    CHECK(!fly_east(65.0f).first);
}

namespace {
const audio_manifest& hub_manifest() {
    static const audio_manifest manifest = *load_audio_manifest_from_file(asset_root() + "/" + sounds_path).manifest;
    return manifest;
}
const audio_sound_definition& manifest_sound(const std::string& id) {
    const auto& sounds = hub_manifest().sounds;
    const auto found = std::find_if(sounds.begin(), sounds.end(), [&id](const audio_sound_definition& sound) { return sound.id == id; });
    static const audio_sound_definition missing;
    CHECK(found != sounds.end());
    return found != sounds.end() ? *found : missing;
}
// The shipped hub as a client online at `now`, its board listing `times`.
game_state online_hub(const std::int64_t now, const std::vector<std::int64_t>& times = {}) {
    auto state = shipped_hub();
    state.play = play_mode::online;
    state.online.server_now = now;
    for (const std::int64_t at : times) {
        departure_info departure; departure.slot_id = static_cast<std::uint64_t>(at); departure.course_id = "jelling_par_3";
        departure.scheduled_at = at; departure.names = {"ANNE"};
        state.online.departures.push_back(departure);
    }
    return state;
}
std::vector<audio_event_type> cues(game_state& state) {
    std::vector<audio_event_type> types;
    for (const auto& event : state.audio_events) { types.push_back(event.type); }
    state.audio_events.clear();
    return types;
}
}

TEST_CASE("a departure boards from bus_early_minutes before its time, last call a minute before, gone at the time") {
    const auto& server = shipped_content().tuning.server;
    const std::int64_t at = 1000 * 15 * minute_micros;
    const std::int64_t early = server.bus_early_minutes * minute_micros;
    const std::int64_t last = static_cast<std::int64_t>(server.last_call_seconds * 1.0e6f);
    CHECK(boarding_phase_at(at, at - early - 1, server) == boarding_phase::waiting);
    CHECK(boarding_phase_at(at, at - early, server) == boarding_phase::boarding);
    CHECK(boarding_phase_at(at, at - last - 1, server) == boarding_phase::boarding);
    CHECK(boarding_phase_at(at, at - last, server) == boarding_phase::last_call);
    CHECK(boarding_phase_at(at, at - 1, server) == boarding_phase::last_call);
    CHECK(boarding_phase_at(at, at, server) == boarding_phase::gone);
    // The last call is a minute, within the boarding window.
    CHECK(server.last_call_seconds == 60.0f); CHECK(last < early);
}

TEST_CASE("the PA calls once when a departure opens, the horn once at its last call, the doors stay open between") {
    const auto& server = shipped_content().tuning.server;
    const std::int64_t at = 1000 * 15 * minute_micros;
    const std::int64_t early = server.bus_early_minutes * minute_micros;
    auto state = online_hub(at - early - 5 * minute_micros, {at});
    REQUIRE(state.area.hub_sounds);
    update_boarding(state);
    CHECK(cues(state).empty()); CHECK(!bus_doors_open(state));
    state.online.server_now = at - early - 1; update_boarding(state);
    CHECK(cues(state).empty()); CHECK(!bus_doors_open(state));
    // The call, from the PA's speaker; the doors open with it.
    state.online.server_now = at - early; update_boarding(state);
    REQUIRE(state.audio_events.size() == 1);
    CHECK(state.audio_events[0].type == audio_event_type::boarding_call);
    CHECK(near(state.audio_events[0].position, state.area.hub_sounds->announcer));
    state.audio_events.clear();
    CHECK(bus_doors_open(state));
    // Not again while it boards.
    for (const std::int64_t before : {early - minute_micros, 10 * minute_micros, 2 * minute_micros}) {
        state.online.server_now = at - before; update_boarding(state);
        CHECK(cues(state).empty()); CHECK(bus_doors_open(state));
    }
    // The last call: the bus's horn, from the bus.
    state.online.server_now = at - static_cast<std::int64_t>(server.last_call_seconds * 1.0e6f); update_boarding(state);
    REQUIRE(state.audio_events.size() == 1);
    CHECK(state.audio_events[0].type == audio_event_type::bus_horn);
    CHECK(near(state.audio_events[0].position, state.area.hub_sounds->bus_horn));
    state.audio_events.clear();
    state.online.server_now = at - 1; update_boarding(state);
    CHECK(cues(state).empty()); CHECK(bus_doors_open(state));
    // It leaves at its time: the doors shut, nothing sounds.
    state.online.server_now = at; update_boarding(state);
    CHECK(cues(state).empty()); CHECK(!bus_doors_open(state));
    // Joining while it boards is silent, but the doors are open.
    auto late = online_hub(at - 5 * minute_micros, {at});
    update_boarding(late);
    CHECK(cues(late).empty()); CHECK(bus_doors_open(late));
}

TEST_CASE("every group's boarding is called, mine beyond the board's rows too; offline the bus is open with a booking") {
    const auto& server = shipped_content().tuning.server;
    const std::int64_t mine = 2000 * 15 * minute_micros;
    const std::int64_t other = mine - 15 * minute_micros;
    auto state = online_hub(other - 20 * minute_micros, {other});
    // A group on the board is called whether or not it is mine.
    state.online.server_now = other - server.bus_early_minutes * minute_micros - 1; update_boarding(state);
    cues(state);
    state.online.server_now += 1; update_boarding(state);
    CHECK(cues(state) == std::vector<audio_event_type>{audio_event_type::boarding_call});
    // My booking is heard on its own time though the board does not list it.
    state.online.reservation = tee_reservation_info{}; state.online.reservation->scheduled_at = mine;
    state.online.server_now = mine - 20 * minute_micros; update_boarding(state); cues(state);
    state.online.server_now = mine - server.bus_early_minutes * minute_micros; update_boarding(state);
    CHECK(cues(state) == std::vector<audio_event_type>{audio_event_type::boarding_call});
    // On a course there is no bus to call, and it is shut.
    state.kind = place_kind::course; update_boarding(state);
    CHECK(cues(state).empty()); CHECK(!bus_doors_open(state));

    auto offline = shipped_hub();
    CHECK(!bus_doors_open(offline));
    offline.offline_booking = tee_reservation_info{};
    CHECK(bus_doors_open(offline));
    update_boarding(offline); CHECK(cues(offline).empty());
}

TEST_CASE("the starter's PA carries over the whole hub, the far bays and the hill tee included") {
    const auto state = shipped_hub();
    REQUIRE(state.area.hub_sounds);
    const auto& call = manifest_sound("pa_boarding_call");
    REQUIRE(call.spread);
    const float meters = state.tuning.scale.meters_per_world_unit;
    bool hill = false;
    for (const auto& mat : state.hub->tee_mats) {
        const float reach = glm::distance(state.area.hub_sounds->announcer, mat.position) * meters;
        CHECK(reach < call.spread->max);
        // Heard, not just inside the limit: a good share of full level.
        sound_listener listener; listener.position = mat.position + glm::vec3(0, 1.7f, 0);
        CHECK(place_sound(*call.spread, state.area.hub_sounds->announcer, listener, meters).gain > 0.1f);
        hill = hill || mat.position.y > 10.0f;
    }
    CHECK(hill);
    // The horn carries as far, and the engine only about the stop.
    CHECK(manifest_sound("bus_horn").spread->max >= call.spread->max);
    CHECK(manifest_sound("bus_engine_loop").spread->max < call.spread->max);
}

TEST_CASE("the hub's ambience: birds in the shelterbelts and chatter on the terrace, as placed loops that exist") {
    const auto state = shipped_hub();
    const auto loops = hub_ambience_loops(state);
    REQUIRE(!loops.empty());
    int birds = 0, chatter = 0;
    for (const auto& loop : loops) {
        const auto& sound = manifest_sound(loop.sound);
        CHECK(sound.type == audio_sound_type::loop); CHECK(sound.spread.has_value());
        birds += loop.sound == "birds_loop"; chatter += loop.sound == "terrace_chatter_loop";
    }
    CHECK(birds >= 4); CHECK(chatter == 1);
    // The chatter is by the clubhouse's terrace tables.
    const auto tables = props_of(state, "hub_terrace_table");
    for (const auto& loop : loops) {
        if (loop.sound != "terrace_chatter_loop") { continue; }
        for (const auto* table : tables) { CHECK(horizontal_distance(loop.position, table->position) < 4.0f); }
    }
    // On a course there is none.
    game_state course = make_game_state(shipped_content(), save_data{});
    CHECK(hub_ambience_loops(course).empty());
}

TEST_CASE("the bus's door leaf swings outward from its front hinge when it boards: seen from the kerb it opens towards the nose") {
    const auto state = shipped_hub();
    const auto bus = props_of(state, "hub_bus");
    REQUIRE(bus.size() == 1);
    const auto& model = shipped_content().hub_props.at("hub_bus");
    const glm::mat4 frame = posed_model(bus[0]->position, world_up, yaw_direction(bus[0]->heading), glm::vec3(1));
    const auto leaf_centre = [&](const bool open) {
        for (const auto& piece : prop_pieces(model, frame, bus_door_pose(model, open ? 1.0f : 0.0f))) {
            if (piece.part->tag == "door" && piece.part->color == model.color("brown")) {
                return glm::vec3(piece.model * glm::vec4(0, 0, 0, 1));
            }
        }
        CHECK(false);
        return glm::vec3(0.0f);
    };
    const auto local = [&](const glm::vec3& world) { return glm::vec3(glm::inverse(frame) * glm::vec4(world, 1)); };
    const auto closed = local(leaf_centre(false)), open = local(leaf_centre(true));
    // Shut it stands in the side; open it is out from it (kerb side is model -x) and at its hinge's end.
    CHECK(closed.x > -1.3f); CHECK(open.x < -1.9f);
    CHECK(open.z > closed.z);
    // On screen, from a step out from the kerb: the open leaf has moved to the nose side.
    const glm::vec3 eye = prop_point(*bus[0], glm::vec3(-9.0f, 1.7f, 0.0f));
    const auto view = glm::perspective(glm::radians(50.0f), 4.0f / 3.0f, .1f, 100.0f) * glm::lookAt(eye, prop_point(*bus[0], glm::vec3(0, 1.5f, 0)), world_up);
    const auto shut_at = project_to_screen(view, leaf_centre(false)), open_at = project_to_screen(view, leaf_centre(true));
    const auto nose = project_to_screen(view, prop_point(*bus[0], glm::vec3(0, 1.5f, 4.3f)));
    REQUIRE(shut_at); REQUIRE(open_at); REQUIRE(nose);
    CHECK((open_at->x - shut_at->x) * (nose->x - shut_at->x) > 0.0f);
    CHECK(glm::distance(eye, leaf_centre(true)) < glm::distance(eye, leaf_centre(false)));
    // The open pose is only the door's: everything else is where it was.
    std::size_t moved = 0;
    const auto shut = prop_pieces(model, frame, bus_door_pose(model, 0.0f)), opened = prop_pieces(model, frame, bus_door_pose(model, 1.0f));
    REQUIRE(shut.size() == opened.size());
    for (std::size_t i = 0; i < shut.size(); ++i) {
        const bool changed = shut[i].model != opened[i].model;
        moved += changed;
        CHECK(changed == (shut[i].part->tag == "door"));
    }
    CHECK(moved > 0);
}

TEST_CASE("the bus's doors take their model's time to swing, in either direction, and stand at the state when there is no swing to continue") {
    const float seconds = shipped_content().hub_props.at("hub_bus").number("door_swing_seconds");
    CHECK(seconds >= 1.0f); CHECK(seconds <= 2.0f);
    CHECK(door_swing_step(0.0f, true, 0.0f, seconds) == 0.0f);
    CHECK(std::abs(door_swing_step(0.0f, true, seconds * 0.25f, seconds) - 0.25f) < 1e-4f);
    CHECK(std::abs(door_swing_step(0.5f, true, seconds * 0.25f, seconds) - 0.75f) < 1e-4f);
    CHECK(std::abs(door_swing_step(0.5f, false, seconds * 0.25f, seconds) - 0.25f) < 1e-4f);
    CHECK(door_swing_step(0.9f, true, seconds, seconds) == 1.0f);
    CHECK(door_swing_step(0.1f, false, seconds, seconds) == 0.0f);
    CHECK(door_swing_step(0.5f, true, -1.0f, seconds) == 0.5f);
    // Joining while it boards, or a new course: no swing, the doors stand open (or shut).
    CHECK(door_swing_step(std::nullopt, true, 0.016f, seconds) == 1.0f);
    CHECK(door_swing_step(std::nullopt, false, 0.016f, seconds) == 0.0f);
    // A swing in many frames adds up to its time.
    float progress = 0.0f;
    for (int frame = 0; frame < 96; ++frame) { progress = door_swing_step(progress, true, seconds / 96.0f, seconds); }
    CHECK(std::abs(progress - 1.0f) < 1e-4f);
}

TEST_CASE("half way through its swing the bus's door leaf is between shut and open, and seen from the kerb it moves steadily towards the nose") {
    const auto state = shipped_hub();
    const auto bus = props_of(state, "hub_bus");
    REQUIRE(bus.size() == 1);
    const auto& model = shipped_content().hub_props.at("hub_bus");
    const glm::mat4 frame = posed_model(bus[0]->position, world_up, yaw_direction(bus[0]->heading), glm::vec3(1));
    // The leaf's free edge and its hinge end, mid-height, in the world.
    const prop_part* leaf = nullptr;
    for (const auto& part : model.parts) {
        if (part.tag == "door" && part.color == model.color("cream")) { leaf = &part; }
    }
    REQUIRE(leaf);
    const auto end = [&](const float progress, const float side) {
        return glm::vec3(frame * bus_door_pose(model, progress).frame * glm::vec4(leaf->at + glm::vec3(0, 0, side * 0.5f * leaf->size.z), 1));
    };
    const auto edge = [&](const float progress) { return end(progress, -1.0f); };
    const glm::vec3 eye = prop_point(*bus[0], glm::vec3(-9.0f, 1.7f, 0.0f));
    const auto view = glm::perspective(glm::radians(50.0f), 4.0f / 3.0f, .1f, 100.0f) * glm::lookAt(eye, prop_point(*bus[0], glm::vec3(0, 1.5f, 0)), world_up);
    const auto nose = project_to_screen(view, prop_point(*bus[0], glm::vec3(0, 1.5f, 4.3f)));
    REQUIRE(nose);
    float previous = 0.0f;
    float away = 0.0f;
    for (const float progress : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const auto at = project_to_screen(view, edge(progress));
        REQUIRE(at);
        if (progress == 0.0f) { previous = at->x; away = nose->x - at->x; continue; }
        // Each step moves the edge nearer the nose side of the screen than before.
        CHECK((at->x - previous) * away > 0.0f);
        previous = at->x;
    }
    // A hinge about its own point: the hinge end stays where it is while the free edge sweeps out.
    CHECK(glm::distance(end(0.5f, 1.0f), end(0.0f, 1.0f)) < 0.01f);
    CHECK(glm::distance(edge(0.5f), edge(0.0f)) > 0.5f);
}

TEST_CASE("the bus stands on a paved bay that runs to the road") {
    const auto& world = shipped_hub().world;
    REQUIRE(world.bus_stop);
    bool found = false;
    for (const auto& road : world.cart_roads) {
        if (road.polyline.size() != 2 || road.width < 4.0f || road.width > 6.0f) { continue; }
        const auto& a = road.polyline.front(); const auto& b = road.polyline.back();
        if (std::abs(a.x - world.bus_stop->position.x) > 0.01f || std::abs(b.x - world.bus_stop->position.x) > 0.01f) { continue; }
        // From past the bus's nose to the road, along its whole length and wider than it.
        found = std::min(a.z, b.z) < world.bus_stop->position.z - 4.4f && std::max(a.z, b.z) >= 78.0f - 0.01f;
    }
    CHECK(found);
}

TEST_CASE("the departure board lists the next groups: in-game time, course, names; a long list or name is shortened to fit the row") {
    auto state = online_hub(12LL * 60 * minute_micros);
    const auto& text = shipped_text_assets();
    state.tuning.sky_clock = sky_timeline{0, 0.0, 6.0, 1989};
    state.hub_sky.utc_offset_hours = 2.0f;
    startup_catalog catalog;
    for (const auto& course : shipped_content().courses) { catalog.courses.push_back({course, 0, std::nullopt}); }
    REQUIRE(catalog.courses.size() >= 4);
    for (int i = 0; i < 4; ++i) {
        departure_info departure; departure.slot_id = static_cast<std::uint64_t>(i + 1);
        departure.course_id = catalog.courses[static_cast<std::size_t>(i)].course.id;
        departure.scheduled_at = state.online.server_now + (15 + 15 * i) * minute_micros;
        departure.names = i == 0 ? std::vector<std::string>{"ANNE", "BO", "CARL"}
            : i == 1 ? std::vector<std::string>{"ABCDEFGHIJKL", "MNOPQRSTUVWX", "YZABCDEFGHIJ", "KLMNOPQRSTUV", "WXYZABCDEFGH"}
            : std::vector<std::string>{"SOLO"};
        state.online.departures.push_back(departure);
    }
    const auto labels = prop_board_labels(state, catalog, text, "");
    std::vector<std::string> rows;
    for (std::size_t i = 0; i < labels.size(); ++i) { if (state.area.props[i].model == departure_row_model) { rows.push_back(labels[i]); } }
    REQUIRE(rows.size() == static_cast<std::size_t>(state.tuning.server.departure_board_rows));
    const auto& row_model = shipped_content().hub_props.at(departure_row_model);
    const int width = prop_board_face_width(glm::vec2(row_model.point("board_size")) * 0.5f);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        // The hub's in-game clock, as the datestamp writes it: never the real UTC time.
        const std::string time = tee_time_text(state.online.departures[i].scheduled_at, state, text);
        CHECK(rows[i].rfind(time, 0) == 0);
        CHECK(rows[i].find("UTC") == std::string::npos);
        CHECK(rows[i].find(course_name(catalog, state.online.departures[i].course_id)) != std::string::npos);
        // Whatever it says is whole on the board: nothing is cut off.
        CHECK(prop_board_label_fits(text, rows[i], width));
    }
    const std::string real_clock = "12:15";
    CHECK(rows[0].rfind(real_clock, 0) != 0);
    CHECK(rows[0].find("\nANNE, BO, CARL") != std::string::npos);
    // Five 12-letter names do not fit: the first and how many more.
    CHECK(rows[1].find("ABCDEFGHIJKL +4") != std::string::npos);
    CHECK(rows[1].find("MNOPQRSTUVWX") == std::string::npos);
    CHECK(rows[2].find("SOLO") != std::string::npos);
    // Every shipped course name with the longest names: whole, or the names shortened.
    for (const auto& course : catalog.courses) {
        departure_info longest; longest.course_id = course.course.id; longest.scheduled_at = state.online.server_now + 15 * minute_micros;
        longest.names = {"ABCDEFGHIJKL", "MNOPQRSTUVWX"};
        const std::string row = departure_text(longest, state, catalog, text, width);
        CHECK(row.rfind(tee_time_text(longest.scheduled_at, state, text), 0) == 0);
        CHECK(row.find(course.course.name) != std::string::npos);
        CHECK(prop_board_label_fits(text, row, width));
    }
    // Fewer groups than rows leaves the rest blank; offline the board says only my booking.
    state.online.departures.resize(1);
    const auto few = prop_board_labels(state, catalog, text, "");
    std::size_t blank = 0;
    for (std::size_t i = 0; i < few.size(); ++i) { blank += state.area.props[i].model == departure_row_model && few[i].empty(); }
    CHECK(blank == rows.size() - 1);
    auto offline = shipped_hub();
    CHECK(departure_row_label(offline, catalog, text, 0).empty());
    offline.offline_booking = tee_reservation_info{}; offline.offline_booking->course_id = catalog.courses[0].course.id;
    CHECK(departure_row_label(offline, catalog, text, 0) == catalog.courses[0].course.name);
    CHECK(departure_row_label(offline, catalog, text, 1).empty());
}

TEST_CASE("hub path lamps use the night lights and the book lies on the hut counter") {
    const auto state = shipped_hub();
    const auto lamps = props_of(state, "hub_path_lamp");
    REQUIRE(lamps.size() > 10);
    render_data frame;
    tree_shadow_cache trees;
    const auto palette = load_lighting(state.asset_root);
    REQUIRE(palette);
    frame.lighting.night_lights = 0;
    add_frame_lights(frame, state, *palette, trees);
    CHECK(frame.lights.empty());
    frame.lighting.night_lights = 1;
    add_frame_lights(frame, state, *palette, trees);
    CHECK(frame.lights.size() >= lamps.size());
    for (const auto* lamp : lamps) {
        const auto head = prop_point(*lamp, state.hub_props.at(lamp->model).point("lamp"));
        CHECK(std::any_of(frame.lights.begin(), frame.lights.end(), [&](const render_light& light) {
            return near(light.position, head, .001f) && light.fixed_lamp && light.color.r > light.color.b;
        }));
    }
    const auto book = props_of(state, "tee_book")[0];
    const auto hut = props_of(state, "hub_starter_hut")[0];
    const auto counter = prop_point(*hut, glm::vec3(0,1.05f,1.62f));
    CHECK(horizontal_distance(book->position, counter) < .05f);
    CHECK(near(book->position.y + 1.05f, counter.y, .05f));
    const auto eye = state.world.hub->book.position + glm::vec3(0,1.6f,0);
    const auto view = glm::perspective(glm::radians(60.f), 4.f/3.f, .1f, 100.f) * glm::lookAt(eye, counter, world_up);
    const auto page = project_to_screen(view, book->position + glm::vec3(0,1.10f,0));
    REQUIRE(page);
    CHECK(std::abs(page->x) < .05f);
    CHECK(std::abs(page->y) < .1f);
}
