#pragma once

#include "core/input.h"
#include "core/window.h"
#include "game/game_content.h"
#include "game/game_state.h"
#include "game/hole_data.h"
#include "renderer/renderer.h"

#include <string>
#include <vector>

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

struct app {
    bool init();
    void run();
    void shutdown();

private:
    window window_;
    renderer renderer_;
    input_state input_;
    game_state game_;
    game_content content_;
    std::vector<startup_hole_option> hole_options_;
    startup_flow startup_flow_ = startup_flow::main;
    int startup_selection_ = 0;
    bool confirm_menu_active_ = false;
    int confirm_selection_ = 1;
    bool show_fps_ = false;
    float fps_elapsed_seconds_ = 0.0f;
    int fps_frame_count_ = 0;
    int displayed_fps_ = 0;
    bool running_ = false;
};
