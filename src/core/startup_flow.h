#pragma once

// Menu flow: main menu, help, hole and course pickers, and the in-round
// "are you sure" menu. Turns input into state changes plus actions and UI
// sounds for app to carry out, and builds the menu render data. No SDL: app
// converts mouse clicks to overlay clip space first. New screens go here.

#include "core/input.h"
#include "game/course_definition.h"
#include "game/game_content.h"
#include "game/hole_data.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

enum class startup_flow {
    main,
    help,
    hole_picker,
    course_picker,
    playing
};

// Main menu items, top to bottom.
enum class main_menu_item {
    play_hole,
    play_course,
    help,
    quit,
    count
};

struct startup_hole_option {
    std::string path;  // relative to the asset root
    hole_data hole;
    // The backdrop of the first course playing this hole; empty when none does.
    std::string backdrop;
};

struct startup_course_option {
    course_definition course;
    int total_par = 0;
    std::optional<render_hole_preview> preview;  // hole 1
};

// Everything the pickers show, loaded once at startup.
struct startup_catalog {
    std::vector<startup_hole_option> holes;
    std::vector<startup_course_option> courses;
};

struct startup_flow_state {
    startup_flow flow = startup_flow::main;
    int selection = 0;
    bool confirm_active = false;
    // 0 = YES, 1 = NO. Opens on NO so a stray key press never quits a round.
    int confirm_selection = 1;
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

struct startup_menu_result {
    startup_action action = startup_action::none;
    // Set for start_course. The flow stays in the menu until app has started
    // it (see enter_playing), so a course that fails to load keeps the menu.
    course_definition course;
    std::vector<ui_sound> sounds;
};

struct confirm_menu_result {
    bool leave_round = false;  // the player confirmed leaving for the main menu
    std::vector<ui_sound> sounds;
};

// Every hole file in <asset_root>/holes (sorted by path) and every course.
startup_catalog load_startup_catalog(const game_content& content);

// `click` is this frame's left click in overlay clip space.
startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click,
                                        const startup_catalog& catalog);
void enter_playing(startup_flow_state& state);
void return_to_main_menu(startup_flow_state& state);

void open_confirm_menu(startup_flow_state& state);
confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click);

render_startup_menu make_startup_menu_render_data(const startup_flow_state& state,
                                                  const startup_catalog& catalog,
                                                  const text_assets& text);
render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state, const text_assets& text);
