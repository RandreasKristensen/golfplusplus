#pragma once

#include "audio/audio_engine.h"
#include "core/input.h"
#include "core/window.h"
#include "game/cloud_save.h"
#include "game/game_content.h"
#include "game/game_state.h"
#include "game/hole_data.h"
#include "game/save_manager.h"
#include "renderer/renderer.h"

#include <string>
#include <vector>

enum class startup_flow {
    main,
    help,
    hole_picker,
    course_picker,
    shop_picker,
    shop_inventory,
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
    void mark_current_save_dirty();
    bool persist_current_save();
    void sync_current_save();

    window window_;
    renderer renderer_;
    audio_engine audio_;
    input_state input_;
    game_state game_;
    game_content content_;
    save_paths save_paths_;
    save_slot save_slot_;
    offline_cloud_save_client cloud_save_;
    std::vector<startup_hole_option> hole_options_;
    startup_flow startup_flow_ = startup_flow::main;
    int startup_selection_ = 0;
    int active_shop_index_ = 0;
    bool confirm_menu_active_ = false;
    int confirm_selection_ = 1;
    bool show_fps_ = false;
    float fps_elapsed_seconds_ = 0.0f;
    int fps_frame_count_ = 0;
    int displayed_fps_ = 0;
    bool running_ = false;
    bool save_initialized_ = false;
};
