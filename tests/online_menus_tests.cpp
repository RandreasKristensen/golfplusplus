#include "doctest.h"
#include "renderer/overlay_batch.h"

#include "core/startup_flow.h"
#include "game/net_types.h"
#include "game/text_ids.h"
#include "renderer/menu_overlay.h"

#include "test_support.h"

#include <optional>
#include <string>
#include <vector>

namespace {
startup_catalog one_course() {
    course_definition course;
    course.id = "first";
    course.name = "First";
    startup_catalog catalog;
    catalog.courses = {startup_course_option{course, 0, std::nullopt}};
    catalog.name_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ";
    catalog.name_max_length = 12;
    return catalog;
}

online_menu_status signed_out() {
    online_menu_status online;
    online.available = true;
    return online;
}

online_menu_status signed_in_as(const std::string& name, const std::uint64_t account = 7) {
    online_menu_status online = signed_out();
    online.status = net_status::connected;
    online.account_id = account;
    online.name = name;
    return online;
}

input_state enter() {
    input_state input;
    input.enter.pressed = true;
    return input;
}

input_state typed(const std::string& text) {
    input_state input;
    input.text_typed = text;
    return input;
}

startup_menu_result update(startup_flow_state& state, const input_state& input, const online_menu_status& online) {
    return update_startup_menu(state, input, menu_pointer{}, one_course(), online);
}

render_startup_menu render(const startup_flow_state& state, const online_menu_status& online) {
    return make_startup_menu_render_data(state, one_course(), shipped_text_assets(), online);
}

bool requested(const startup_menu_result& result, const online_request_type type, const std::string& text = {}) {
    for (const online_request& request : result.requests) {
        if (request.type == type && request.text == text) {
            return true;
        }
    }
    return false;
}

// Signed in on a new account, at name entry.
startup_flow_state at_name_entry() {
    startup_flow_state state;
    open_online_login(state);
    update(state, input_state{}, signed_in_as(""));
    return state;
}
}

TEST_CASE("PLAY ONLINE signs in silently first, and only opens the browser when asked") {
    startup_flow_state state;
    startup_menu_result result = update(state, enter(), signed_out());
    CHECK(state.flow == startup_flow::online_login);
    CHECK(requested(result, online_request_type::begin_silent_login));
    CHECK(!requested(result, online_request_type::begin_login));

    online_menu_status online = signed_out();
    online.status = net_status::signing_in;
    online.failure_id.clear();
    render_startup_menu menu = render(state, online);
    CHECK(menu.message == "SIGNING IN...");
    REQUIRE(menu.tiles.size() == 1U);
    CHECK(menu.tiles[0].title == "CANCEL");

    // No stored sign-in is not an error: the player just signs in.
    online.status = net_status::failed;
    online.failure_id = net_failure_no_stored_sign_in;
    menu = render(state, online);
    CHECK(menu.message == "SIGN IN WITH YOUR BROWSER");
    CHECK(menu.severity == message_severity::status);
    REQUIRE(menu.tiles.size() == 2U);
    CHECK(menu.tiles[0].title == "SIGN IN");
    result = update(state, enter(), online);
    CHECK(requested(result, online_request_type::begin_login));

    online.status = net_status::waiting_for_browser;
    online.failure_id.clear();
    CHECK(render(state, online).message == "CONTINUE IN YOUR BROWSER");

    // A real failure says why and offers a retry.
    online.status = net_status::failed;
    online.failure_id = "connect_failed";
    menu = render(state, online);
    CHECK(menu.message == "CANNOT REACH THE SERVER");
    CHECK(menu.severity == message_severity::error);
    CHECK(menu.tiles[0].title == "RETRY");

    update(state, input_state{}, signed_in_as(""));
    CHECK(state.flow == startup_flow::name_entry);
    CHECK(wants_text_input(state));
}

TEST_CASE("cancelling sign-in stops it and goes back to the main menu") {
    startup_flow_state state;
    open_online_login(state);
    online_menu_status online = signed_out();
    online.status = net_status::waiting_for_browser;
    input_state escape;
    escape.escape.pressed = true;
    const startup_menu_result result = update(state, escape, online);
    CHECK(requested(result, online_request_type::cancel_login));
    CHECK(state.flow == startup_flow::main);
}

TEST_CASE("without online play the sign-in screen only says so") {
    startup_flow_state state;
    const startup_menu_result result = update(state, enter(), online_menu_status{});
    CHECK(state.flow == startup_flow::online_login);
    CHECK(result.requests.empty());
    const render_startup_menu menu = render(state, online_menu_status{});
    CHECK(menu.severity == message_severity::error);
    REQUIRE(menu.tiles.size() == 1U);
    CHECK(menu.tiles[0].title == "BACK");
}

TEST_CASE("name entry claims the typed name, shows refusals inline and moves on once named") {
    startup_flow_state state = at_name_entry();
    REQUIRE(state.flow == startup_flow::name_entry);
    const online_menu_status nameless = signed_in_as("");

    update(state, typed("anna!"), nameless);
    CHECK(state.field.value == "ANNA");
    // Backspace edits the name; it does not leave the screen.
    input_state backspace;
    backspace.backspace.pressed = true;
    update(state, backspace, nameless);
    CHECK(state.field.value == "ANN");
    CHECK(state.flow == startup_flow::name_entry);

    // Space types a space; only Enter submits.
    input_state space = typed(" A");
    space.space.pressed = true;
    startup_menu_result result = update(state, space, nameless);
    CHECK(state.field.value == "ANN A");
    CHECK(result.requests.empty());

    result = update(state, enter(), nameless);
    CHECK(requested(result, online_request_type::claim_name, "ANN A"));
    CHECK(state.waiting);
    CHECK(render(state, nameless).message == "CHECKING THE NAME...");
    // Not sent twice while waiting.
    CHECK(update(state, enter(), nameless).requests.empty());

    online_menu_status refused = nameless;
    refused.reducer_failures = {reducer_failure{"claim_name", "name_taken"}};
    update(state, input_state{}, refused);
    CHECK(!state.waiting);
    const render_startup_menu menu = render(state, nameless);
    CHECK(menu.message == "NAME TAKEN");
    CHECK(menu.severity == message_severity::error);
    REQUIRE(menu.field.has_value());
    CHECK(menu.field->value == "ANN A");

    update(state, input_state{}, signed_in_as("ANN A"));
    CHECK(state.flow == startup_flow::joining);
    CHECK(!wants_text_input(state));
}

TEST_CASE("a guest's name entry has no way to link an account") {
    startup_flow_state state = at_name_entry();
    online_menu_status guest = signed_in_as("");
    guest.guest = true;
    CHECK(render(state, guest).tiles.size() == 1U);

    input_state down_and_enter = enter();
    down_and_enter.down.pressed = true;
    update(state, typed("ANNA"), guest);
    const startup_menu_result result = update(state, down_and_enter, guest);
    CHECK(state.flow == startup_flow::name_entry);
    CHECK(requested(result, online_request_type::claim_name, "ANNA"));
}

TEST_CASE("a link code from another login switches to that account") {
    startup_flow_state state = at_name_entry();
    const online_menu_status nameless = signed_in_as("");
    input_state down_and_enter = enter();
    down_and_enter.down.pressed = true;
    update(state, down_and_enter, nameless);
    REQUIRE(state.flow == startup_flow::link_code_entry);

    // Only the code alphabet, uppercased, up to the code length.
    update(state, typed("abc-0 1d2345678"), nameless);
    CHECK(state.field.value == "ABCD2345");

    online_menu_status online = nameless;
    online.link_results = 3;
    startup_menu_result result = update(state, enter(), online);
    CHECK(requested(result, online_request_type::redeem_link_code, "ABCD2345"));
    CHECK(state.waiting);

    online.link_results = 4;
    online.link_result = link_result_invalid_code;
    update(state, input_state{}, online);
    CHECK(!state.waiting);
    CHECK(render(state, online).message == "WRONG OR EXPIRED CODE");

    update(state, enter(), online);
    REQUIRE(state.waiting);
    // Linked: the result can come before the account changes.
    online.link_results = 5;
    online.link_result = link_result_linked;
    update(state, input_state{}, online);
    CHECK(state.waiting);
    CHECK(state.flow == startup_flow::link_code_entry);

    update(state, input_state{}, signed_in_as("BOB", 9));
    CHECK(state.flow == startup_flow::joining);
}

TEST_CASE("online entry sends only a hub join and waits for the complete hub room") {
    startup_flow_state state;
    const online_menu_status named = signed_in_as("ANNA");
    auto result = update(state, enter(), named);
    REQUIRE(state.flow == startup_flow::joining);
    CHECK(requested(result, online_request_type::join_course, online_hub_course));
    CHECK(update(state, input_state{}, named).action == startup_action::none);
    online_menu_status in_room = named;
    in_room.room_course_id = online_hub_course;
    result = update(state, input_state{}, in_room);
    CHECK(result.action == startup_action::start_course);
    CHECK(result.play == play_mode::online);
}
TEST_CASE("a refused or cancelled hub entry returns to main") {
    startup_flow_state state;
    const auto named = signed_in_as("ANNA");
    update(state, enter(), named);
    auto refused = named;
    refused.reducer_failures = {reducer_failure{"join_course", "unknown_course"}};
    update(state, input_state{}, refused);
    CHECK(state.flow == startup_flow::main);
    update(state, enter(), named);
    input_state escape; escape.escape.pressed = true;
    const auto result = update(state, escape, named);
    CHECK(requested(result, online_request_type::leave_room));
    CHECK(state.flow == startup_flow::main);
}

TEST_CASE("losing the connection on an online screen goes back to signing in") {
    startup_flow_state state = at_name_entry();
    online_menu_status online = signed_in_as("");
    online.status = net_status::signing_in;
    update(state, typed("A"), online);
    CHECK(state.flow == startup_flow::online_login);
    CHECK(!wants_text_input(state));
}

TEST_CASE("LINK ANOTHER LOGIN shows the code, asking for one only when there is none") {
    startup_flow_state state;
    state.selection = 3;
    online_menu_status online = signed_in_as("ANNA");
    startup_menu_result result = update(state, enter(), online);
    CHECK(state.flow == startup_flow::link_code_show);
    CHECK(requested(result, online_request_type::create_link_code));
    CHECK(render(state, online).message == "MAKING A CODE...");

    online.link_code = "ABCD2345";
    online.link_code_minutes_left = 5;
    const render_startup_menu menu = render(state, online);
    CHECK(menu.code == "ABCD2345");
    CHECK(menu.message == "EXPIRES IN 5 MIN");

    return_to_main_menu(state);
    state.selection = 3;
    result = update(state, enter(), online);
    CHECK(state.flow == startup_flow::link_code_show);
    CHECK(result.requests.empty());

    // Refused: says why, and does not ask again.
    online.link_code.clear();
    online.reducer_failures = {reducer_failure{"create_link_code", "linking_off"}};
    update(state, input_state{}, online);
    online.reducer_failures.clear();
    result = update(state, input_state{}, online);
    CHECK(result.requests.empty());
    CHECK(render(state, online).message == "LINKING LOGINS IS OFF ON THIS SERVER");
}

TEST_CASE("replacing a menu message replaces its severity: a retry never inherits an earlier error") {
    startup_flow_state state;
    open_online_login(state);
    state.message_key = "online.error.connect_failed";
    online_menu_status online = signed_out();
    online.status = net_status::failed;
    online.failure_id = "connect_failed";
    // The failure shows as an error ...
    CHECK(render(state, online).severity == message_severity::error);
    // ... and the retry in progress, which replaces the text, is a status.
    online.status = net_status::signing_in;
    online.failure_id.clear();
    render_startup_menu menu = render(state, online);
    CHECK(menu.message == "SIGNING IN...");
    CHECK(menu.severity == message_severity::status);
    online.status = net_status::waiting_for_browser;
    CHECK(render(state, online).severity == message_severity::status);

    // Joining after an error says so as a status too.
    startup_flow_state joining;
    open_online_login(joining);
    joining.flow = startup_flow::joining;
    joining.message_key = "online.error.name_taken";
    menu = render(joining, signed_in_as("ME"));
    CHECK(!menu.message.empty());
    CHECK(menu.severity == message_severity::status);

    // The text drawn for each severity differs: an error uses the error style, a status the subtitle.
    const text_assets& text = shipped_text_assets();
    for (const message_severity severity : {message_severity::status, message_severity::error}) {
        render_startup_menu shown;
        shown.screen = startup_menu_screen::form;
        set_message(shown, "SAMPLE", severity);
        overlay_batch batch;
        batch.grid = overlay_grid{854, 480};
        draw_startup_menu(batch, text, shown, test_scene_colors().ground);
        const glm::vec3 error_colour(0.94f, 0.38f, 0.26f);
        bool has_error_colour = false;
        for (const overlay_vertex& vertex : batch.vertices) {
            has_error_colour = has_error_colour || glm::vec3(vertex.color) == error_colour;
        }
        CHECK(has_error_colour == (severity == message_severity::error));
    }
}
