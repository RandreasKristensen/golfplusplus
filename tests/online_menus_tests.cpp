#include "doctest.h"

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
    return update_startup_menu(state, input, std::nullopt, one_course(), online);
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
    render_startup_menu menu = render(state, online);
    CHECK(menu.message == "SIGNING IN...");
    REQUIRE(menu.tiles.size() == 1U);
    CHECK(menu.tiles[0].title == "CANCEL");

    // No stored sign-in is not an error: the player just signs in.
    online.status = net_status::failed;
    online.failure_id = net_failure_no_stored_sign_in;
    menu = render(state, online);
    CHECK(menu.message == "SIGN IN WITH YOUR BROWSER");
    CHECK(!menu.message_is_error);
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
    CHECK(menu.message_is_error);
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
    CHECK(menu.message_is_error);
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
    CHECK(menu.message_is_error);
    REQUIRE(menu.field.has_value());
    CHECK(menu.field->value == "ANN A");

    update(state, input_state{}, signed_in_as("ANN A"));
    CHECK(state.flow == startup_flow::online_course_picker);
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
    CHECK(state.flow == startup_flow::online_course_picker);
}

TEST_CASE("joining a course starts it online once the room is there") {
    startup_flow_state state;
    const online_menu_status named = signed_in_as("ANNA");
    update(state, enter(), named);
    REQUIRE(state.flow == startup_flow::online_course_picker);

    startup_menu_result result = update(state, enter(), named);
    CHECK(requested(result, online_request_type::join_course, "first"));
    CHECK(state.flow == startup_flow::joining);
    CHECK(render(state, named).message == "JOINING First...");

    result = update(state, input_state{}, named);
    CHECK(result.action == startup_action::none);

    online_menu_status in_room = named;
    in_room.room_course_id = "first";
    result = update(state, input_state{}, in_room);
    CHECK(result.action == startup_action::start_course);
    CHECK(result.play == play_mode::online);
    CHECK(result.course.id == "first");
}

TEST_CASE("a refused join goes back to the course picker and says why") {
    startup_flow_state state;
    const online_menu_status named = signed_in_as("ANNA");
    update(state, enter(), named);
    update(state, enter(), named);
    REQUIRE(state.flow == startup_flow::joining);

    online_menu_status refused = named;
    refused.reducer_failures = {reducer_failure{"join_course", "unknown_course"}};
    update(state, input_state{}, refused);
    CHECK(state.flow == startup_flow::online_course_picker);
    const render_startup_menu menu = render(state, named);
    CHECK(menu.message == "NO SUCH COURSE ON THIS SERVER");

    // Cancelling a join leaves the room it may have got.
    update(state, enter(), named);
    input_state escape;
    escape.escape.pressed = true;
    const startup_menu_result result = update(state, escape, named);
    CHECK(requested(result, online_request_type::leave_room));
    CHECK(state.flow == startup_flow::online_course_picker);
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
    state.selection = 2;
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
    state.selection = 2;
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
