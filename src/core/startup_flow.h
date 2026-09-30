#pragma once

// Startup menu flow: main menu, help, hole/course pickers and the in-game
// "are you sure" confirm menu. Owns menu selection logic and builds the menu
// render data; app owns the game state, audio and the window. No SDL here:
// app converts mouse clicks to overlay clip space first.

#include <optional>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "core/input.h"
#include "game/course_definition.h"
#include "game/game_content.h"
#include "game/hole_data.h"
#include "game/text_assets.h"
#include "renderer/menu_overlay.h"

enum class startup_flow {
    main,
    help,
    hole_picker,
    course_picker,
    playing
};

struct startup_hole_option {
    std::string path;
    hole_data hole;
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
    // True when the player confirmed leaving the round for the main menu.
    bool leave_round = false;
    std::vector<ui_sound> sounds;
};

// Every hole file in <asset_root>/holes, sorted by path, for the hole picker.
std::vector<startup_hole_option> load_startup_holes(const std::string& asset_root);

// Main-menu input. `click` is this frame's left click in overlay clip space.
startup_menu_result update_startup_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click,
                                        const std::vector<startup_hole_option>& holes,
                                        const game_content& content);
void enter_playing(startup_flow_state& state);
void return_to_main_menu(startup_flow_state& state);

void open_confirm_menu(startup_flow_state& state);
confirm_menu_result update_confirm_menu(startup_flow_state& state,
                                        const input_state& input,
                                        std::optional<glm::vec2> click);

render_startup_menu make_startup_menu_render_data(const startup_flow_state& state,
                                                  const std::vector<startup_hole_option>& holes,
                                                  const game_content& content,
                                                  const text_assets& text);
render_startup_menu make_confirm_menu_render_data(const startup_flow_state& state, const text_assets& text);
