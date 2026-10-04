#pragma once

// Owns everything (window, renderer, audio, content, game state) and runs the
// main loop: poll input -> update menus or the game -> render.

#include "audio/audio_engine.h"
#include "core/input.h"
#include "core/startup_flow.h"
#include "core/startup_options.h"
#include "core/window.h"
#include "game/game_content.h"
#include "game/game_state.h"
#include "game/text_assets.h"
#include "profiling/profiling.h"
#include "renderer/camera_transition.h"
#include "renderer/render_data.h"
#include "renderer/render_mesh.h"
#include "renderer/renderer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#if GOLFPP_NET
#include "core/online_session.h"
#endif

class app {
public:
    // False (after logging why) when the window, renderer or content fail.
    bool init(const startup_options& options);
    void run();
    void shutdown();

private:
    // A fresh state on the first course as the menu backdrop, keeping the save.
    bool reset_to_menu_backdrop();
    void return_to_menu();
    void save_progress();
    void update_menu(frame_profile* profile);
    void update_confirm_menu(frame_profile* profile);
    void update_round(float dt, frame_profile* profile);
    // How online play is going for the menus; offline only without it.
    online_menu_status online_status() const;
    void carry_out(const std::vector<online_request>& requests);
    // Keeps an online round in its room: restarts it after a reconnect,
    // and leaves for the menus when the connection or the room is gone.
    void watch_online_round();
    void play_game_audio();
    void refresh_render_meshes();
    // `snap_camera` cuts to the live view instead of blending (menus).
    render_data make_frame(float dt, bool snap_camera, frame_profile* profile);
    void present_frame(render_data& data, frame_profile* profile);

    window window_;
    renderer renderer_;
    audio_engine audio_;
    input_state input_;
    game_content content_;
    text_assets text_;
    startup_catalog catalog_;
    startup_flow_state menu_;
    game_state game_;
    // When the existing save could not be read and could not be backed up,
    // saving is disabled so the unreadable file is never overwritten.
    bool saving_enabled_ = true;
    std::filesystem::path save_path_;

    // Presentation state, never read by game logic.
    camera_transition_state camera_transition_;
    std::uint64_t render_meshes_revision_ = 0;
    render_static_mesh terrain_render_mesh_;
    render_static_mesh material_overlay_render_mesh_;
    bool cart_loop_playing_ = false;

#if GOLFPP_NET
    // Online play; updated every frame, menus included, so signing in runs
    // while they show.
    std::optional<online_session> online_;
#endif

    // Ctrl toggles the FPS and profiling overlay; the profiler only records
    // while it is shown.
    profiler profiler_;
    bool running_ = false;
};
