#pragma once

#include "audio/audio_engine.h"
#include "core/input.h"
#include "core/startup_options.h"
#include "core/window.h"
#include "game/game_content.h"
#include "game/game_state.h"
#include "game/hole_data.h"
#include "game/save_manager.h"
#include "profiling/profiling.h"
#include "renderer/renderer.h"

#include <cstdint>
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

// Renderable copy of the static anchor cache's trees. Rebuilt only when that
// cache is (see refresh_render_tree_cache), so the render path borrows it
// instead of rebuilding a tree vector every frame.
struct render_tree_cache {
    std::vector<render_tree> trees;
    std::uint64_t revision = 0;
    std::size_t source_count = 0;
    bool valid = false;
};

struct app {
    // `options` carries profiling-only startup switches (vsync, boot course).
    // They are consumed here and never reach game state or the renderer.
    bool init(const startup_options& options = startup_options{});
    void run();
    void shutdown();

private:
    void mark_current_save_dirty();
    bool persist_current_save();
    // Startup-only: boots straight into the course with this id when it exists.
    // Returns false when no course matched, so the menu is shown instead.
    bool boot_into_course(const std::string& course_id);
    void refresh_render_mesh_cache(frame_profile* profile);
    void present_frame(render_data& data, frame_profile* profile);

    window window_;
    renderer renderer_;
    audio_engine audio_;
    input_state input_;
    game_state game_;
    game_content content_;
    save_paths save_paths_;
    save_slot save_slot_;
    std::vector<startup_hole_option> hole_options_;
    startup_flow startup_flow_ = startup_flow::main;
    int startup_selection_ = 0;
    bool confirm_menu_active_ = false;
    int confirm_selection_ = 1;
    bool show_fps_ = false;
    // Owned here, never global. Handed out as a nullable frame_profile*.
    profiler profiler_;
    render_tree_cache render_trees_;
    std::uint64_t cached_terrain_revision_ = 0;
    render_static_mesh cached_terrain_mesh_;
    render_static_mesh cached_material_overlay_mesh_;
    float fps_elapsed_seconds_ = 0.0f;
    int fps_frame_count_ = 0;
    int displayed_fps_ = 0;
    int displayed_frame_ms_ = 0;
    bool running_ = false;
    bool save_initialized_ = false;
};
