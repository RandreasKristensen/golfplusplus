#pragma once

// Menu flow: main menu, help, settings (core/settings_menu.h), the offline
// pickers, the online screens (core/online_menus.h) and the in-round "leave
// the round?" menu, which also opens settings. Turns input
// into state changes plus actions, online requests and UI sounds for app to
// carry out, and builds the menu render data. No SDL and no network: app
// converts mouse clicks to overlay clip space first, and tells the flow how
// the connection is (online_menu_status). New screens go here.

#include "core/input.h"
#include "game/course_definition.h"
#include "game/game_content.h"
#include "game/hole_data.h"
#include "game/net_types.h"
#include "game/settings.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"
#include "renderer/text_input.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

enum class startup_flow {
    main,
    help,
    settings,
    offline,  // PLAY OFFLINE: a course or a practice hole
    hole_picker,
    course_picker,
    online_login,          // PLAY ONLINE: signing in, until my account is known
    name_entry,            // a new account picks its name...
    link_code_entry,       // ...or links this login to an account it already has
    link_code_show,        // LINK ANOTHER LOGIN: the code to enter on the other login
    online_course_picker,
    joining,               // join_course sent, waiting for the room
    playing
};

// Main menu items, top to bottom. LINK ANOTHER LOGIN and SIGN OUT only show
// while signed in.
enum class main_menu_item {
    play_online,
    play_offline,
    link_login,
    sign_out,
    help,
    settings,
    quit
};

// PLAY OFFLINE's items, top to bottom.
enum class offline_menu_item {
    play_course,
    play_hole,
    count
};

struct startup_hole_option {
    std::string path;  // relative to the asset root
    hole_data hole;
    // The backdrop of the first course playing this hole; empty images when
    // none does.
    course_backdrop backdrop;
};

struct startup_course_option {
    course_definition course;
    int total_par = 0;
    std::optional<render_hole_preview> preview;  // hole 1
};

// Everything the menus show, loaded once at startup.
struct startup_catalog {
    std::vector<startup_hole_option> holes;
    std::vector<startup_course_option> courses;
    // What name entry accepts: the font's characters (the server decides
    // which of them a name may use), up to the server's longest name.
    std::string name_chars;
    std::size_t name_max_length = 0;
    std::vector<setting_definition> settings;  // the settings screen's entries
};

// How online play is going, as the menus need it. app fills it each frame
// from net_client and game_state::online; the default is a game without
// online play.
struct online_menu_status {
    bool available = false;  // online play is built in and configured
    net_status status = net_status::signed_out;
    bool guest = false;  // signed in anonymously: guests cannot link logins
    std::string failure_id;  // why sign-in or the connection failed; empty when it did not
    std::uint64_t account_id = 0;  // 0 until my account arrives
    std::string name;              // empty until claimed
    std::string room_course_id;    // empty outside a room
    std::string link_code;         // my link code; empty when there is none
    int link_code_minutes_left = 0;
    std::string link_result;       // how the last redeem_link_code went
    std::uint64_t link_results = 0;  // changes with every outcome
    std::vector<reducer_failure> reducer_failures;  // refused this frame
    double clock_seconds = 0.0;  // any running clock: the text cursor blinks with it
};

// Signed in with an account (named or not).
bool signed_in(const online_menu_status& online);

struct startup_flow_state {
    startup_flow flow = startup_flow::main;
    int selection = 0;
    bool confirm_active = false;
    // 0 = YES, 1 = NO, 2 = SETTINGS. Opens on NO so a stray key press never
    // quits a round.
    int confirm_selection = 1;
    // The in-round menu shows settings instead, with this selection.
    bool confirm_settings = false;
    int settings_selection = 0;
    // The player's settings: app loads them, applies them when a menu
    // reports settings_changed, and saves them. Kept across every screen.
    settings_values settings;

    // A string table key to show in the screen's message line (errors);
    // empty when none. Cleared when the screen changes.
    std::string message_key;
    // The online screens' text field: a name or a link code.
    text_input_state field;
    // A name, code or link code request was sent and is not answered yet.
    bool waiting = false;
    // When a link code was sent: a change in either means it was answered.
    std::uint64_t account_at_submit = 0;
    std::uint64_t link_results_at_submit = 0;
    course_definition joining_course;
    double clock_seconds = 0.0;
};

enum class ui_sound {
    move,
    select,
    back
};

enum class startup_action {
    none,
    quit,
    start_course
};

// What app asks of net_client for the online screens.
enum class online_request_type {
    begin_silent_login,
    begin_login,
    cancel_login,
    sign_out,
    claim_name,
    redeem_link_code,
    create_link_code,
    join_course,
    leave_room
};

struct online_request {
    online_request_type type = online_request_type::begin_login;
    std::string text;  // claim_name: the name; redeem_link_code: the code; join_course: the course id
};

struct startup_menu_result {
    startup_action action = startup_action::none;
    // Set for start_course. The flow stays in the menu until app has started
    // it (see enter_playing), so a course that fails to load keeps the menu.
    course_definition course;
    play_mode play = play_mode::offline;
    std::vector<online_request> requests;
    std::vector<ui_sound> sounds;
    bool settings_changed = false;
};

struct confirm_menu_result {
    bool leave_round = false;  // the player confirmed leaving for the main menu
    std::vector<ui_sound> sounds;
    bool settings_changed = false;
};

// Every hole file in <asset_root>/holes (sorted by path) and every course,
// with name entry's rules from the font and the server tuning, and the
// settings screen's entries.
startup_catalog load_startup_catalog(const game_content& content, const text_assets& text);

// `click` is this frame's left click in overlay clip space.
startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click,
                                        const startup_catalog& catalog,
                                        const online_menu_status& online = {});
void enter_playing(startup_flow_state& state);
void return_to_main_menu(startup_flow_state& state);
// Back to the online sign-in screen, which shows how the connection is.
void open_online_login(startup_flow_state& state);
// Whether the screen has a text field, so app turns typing on.
bool wants_text_input(const startup_flow_state& state);

void open_confirm_menu(startup_flow_state& state);
confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click,
                                        const std::vector<setting_definition>& settings);

render_startup_menu make_startup_menu_render_data(const startup_flow_state& state,
                                                  const startup_catalog& catalog,
                                                  const text_assets& text,
                                                  const online_menu_status& online = {});
render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state,
                                                  const text_assets& text,
                                                  const std::vector<setting_definition>& settings);
