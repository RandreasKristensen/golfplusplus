#include "core/app.h"

#include "audio/audio_manifest.h"
#include "audio/sound_ids.h"
#include "core/event_loop.h"
#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "game/asset_resolver.h"
#include "game/content_files.h"
#include "game/course_session.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/save_manager.h"
#include "game/settings.h"
#include "game/text_ids.h"
#include "renderer/terrain_render_mesh.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/vec2.hpp>

namespace {
constexpr int window_width = 1280;
constexpr int window_height = 720;
// Below this speed the cart is parked and its engine loop stops.
constexpr float cart_engine_idle_speed = 0.05f;

std::string sdl_path(char* path) {
    const std::string result = path != nullptr ? path : "";
    SDL_free(path);
    return result;
}

// This frame's left click in overlay clip space (-1..1, y up), if any.
std::optional<glm::vec2> mouse_click_position(const input_state& input, SDL_Window* window) {
    if (!input.mouse_left.pressed || window == nullptr) {
        return std::nullopt;
    }
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    const float x = static_cast<float>(input.mouse_x) / static_cast<float>(std::max(1, width)) * 2.0f - 1.0f;
    const float y = 1.0f - static_cast<float>(input.mouse_y) / static_cast<float>(std::max(1, height)) * 2.0f;
    return glm::vec2(x, y);
}

void play_ui_sounds(audio_engine& audio, const std::vector<ui_sound>& sounds) {
    for (const ui_sound sound : sounds) {
        switch (sound) {
        case ui_sound::move:
            audio.play(sound_ui_move);
            break;
        case ui_sound::select:
            audio.play(sound_ui_select);
            break;
        case ui_sound::back:
            audio.play(sound_ui_back);
            break;
        }
    }
}

std::string sound_for_event(const audio_event& event) {
    switch (event.type) {
    case audio_event_type::swing_start:
        return sound_swing_start;
    case audio_event_type::club_hit:
        return event.club_hit_sound;
    case audio_event_type::ball_land:
        if (event.material == terrain_material::water) {
            return sound_ball_splash;
        }
        return event.material == terrain_material::bunker ? sound_ball_land_bunker : sound_ball_land_grass;
    case audio_event_type::ball_tree_hit:
        return sound_ball_tree_hit;
    case audio_event_type::ball_cup:
        return sound_ball_cup;
    case audio_event_type::club_change:
        return sound_club_change;
    case audio_event_type::cart_start:
        return sound_cart_start;
    case audio_event_type::cart_drift:
        return sound_cart_drift;
    case audio_event_type::emote_smoke:
        return sound_emote_smoke;
    case audio_event_type::emote_beer:
        return sound_emote_beer;
    }
    return {};
}

const course_definition* find_course(const game_content& content, const std::string& id) {
    const auto it = std::find_if(content.courses.begin(), content.courses.end(),
                                 [&id](const course_definition& course) { return course.id == id; });
    return it != content.courses.end() ? &*it : nullptr;
}
}

bool app::init(const startup_options& options) {
    const std::string asset_root = resolve_asset_root(sdl_path(SDL_GetBasePath()));
    if (!window_.init("golf++", window_width, window_height, options.vsync, asset_root + "/icons/golfpp-icon.bmp")) {
        return false;
    }
    if (!renderer_.init(window_.sdl_window(), asset_root)) {
        SDL_Log("Renderer init failed (shaders in %s/shaders)", asset_root.c_str());
        return false;
    }

    std::optional<text_assets> text = load_text_assets(asset_root);
    if (!text) {
        SDL_Log("Failed to load text assets (%s, %s, %s) from %s",
                text_font_path, text_strings_path, text_styles_path, asset_root.c_str());
        return false;
    }
    text_ = std::move(*text);
    // The title card stays up while the rest loads, instead of a black window.
    renderer_.render_loading_screen(text_);
    window_.swap();

    game_content_load_result content = load_game_content(asset_root);
    if (!content.content) {
        SDL_Log("Failed to load game content: %s", content.error.c_str());
        return false;
    }
    content_ = std::move(*content.content);
    catalog_ = load_startup_catalog(content_, text_);

#if GOLFPP_NET
    const std::optional<std::string> online_text = read_text_file(std::filesystem::path(asset_root) / online_config_path);
    const std::optional<online_config> online = online_text ? parse_online_config_from_text(*online_text) : std::nullopt;
    // Offline always works: without online play's config or a matching
    // bridge the game starts offline only.
    std::optional<net_client> net;
    if (online) {
        net = net_client::create(
            with_overrides(*online, options.online_server, options.online_database, options.online_anonymous),
            lookup_text(text_, text_online_browser_signed_in), lookup_text(text_, text_online_browser_failed));
    }
    if (net) {
        online_.emplace(std::move(*net));
    } else if (!online) {
        SDL_Log("Online play is off: cannot load %s from %s", online_config_path, asset_root.c_str());
    } else {
        SDL_Log("Online play is off: the client bridge does not match src/net/stdb_bridge.h; rebuild it");
    }
#endif

    const std::string pref_path = sdl_path(SDL_GetPrefPath("golfplusplus", "golf++"));
    const std::filesystem::path save_root =
        pref_path.empty() ? std::filesystem::path(asset_root) / "saves" : std::filesystem::path(pref_path);
    save_path_ = save_file_path(save_root);
    settings_path_ = settings_file_path(save_root);
    menu_.settings = load_settings(settings_path_, content_.settings);
    const save_load_result save = load_save(save_path_);
    if (save.existing_was_unreadable) {
        if (save.unreadable_backup.empty()) {
            saving_enabled_ = false;
            SDL_Log("Save %s is unreadable and could not be backed up; progress will not be saved this session.",
                    save_path_.string().c_str());
        } else {
            SDL_Log("Save %s is unreadable; kept a copy at %s and started a new save.",
                    save_path_.string().c_str(), save.unreadable_backup.string().c_str());
        }
    }
    game_ = make_game_state(content_, save.save);

    // GOLFPP_COURSE skips the menu (profiling); an unknown id shows the menu.
    const course_definition* boot_course =
        options.boot_course_id.empty() ? nullptr : find_course(content_, options.boot_course_id);
    if (!options.boot_course_id.empty() && boot_course == nullptr) {
        SDL_Log("GOLFPP_COURSE=%s: no such course, starting at the menu", options.boot_course_id.c_str());
    }
    if (boot_course != nullptr && start_course(game_, *boot_course)) {
        enter_playing(menu_);
    } else if (!reset_to_menu_backdrop()) {
        SDL_Log("The first course (%s) failed to load", content_.courses.front().id.c_str());
        return false;
    }

    // Typing only reaches input_state::text_typed while a text field is focused.
    set_text_input_enabled(false);
    audio_.init();
    audio_.load_manifest(std::filesystem::path(asset_root) / "audio" / "sounds.json");
    apply_settings(false);
    audio_.start_ambience(menu_.flow == startup_flow::playing ? sound_ambience_course_day : sound_ambience_menu_vcr);
    running_ = true;
    return true;
}

void app::run() {
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running_) {
        const Uint64 counter = SDL_GetPerformanceCounter();
        const float dt = static_cast<float>(static_cast<double>(counter - previous_counter) / counter_frequency);
        previous_counter = counter;

        input_ = poll_events(input_);
        if (input_.quit_requested) {
            running_ = false;
        }
        if (input_.ctrl.pressed) {
            profiler_.enabled = !profiler_.enabled;
        }
        profiler_end_frame(profiler_, dt);
        profiler_begin_frame(profiler_);
        frame_profile* profile = profiler_frame(profiler_);

        if (menu_.flow != startup_flow::playing) {
            update_menu(profile);
        } else if (round_results_shown(game_)) {
            // Online the server has started the next round already; offline
            // the round ends at the menu.
            const bool next = input_.enter.pressed || input_.space.pressed;
            if (next && is_online(game_)) {
                audio_.play(sound_ui_select);
                start_next_round(game_);
            } else if (next || input_.escape.pressed || input_.backspace.pressed) {
                audio_.play(sound_ui_select);
                return_to_menu();
            }
            render_data data = make_frame(dt, menu_.flow != startup_flow::playing, profile);
            present_frame(data, profile);
        } else if (menu_.confirm_active || (input_.escape.pressed && game_.mode == game_mode::walking)) {
            update_confirm_menu(profile);
        } else {
            update_round(dt, profile);
        }
#if GOLFPP_NET
        if (online_) {
            online_->update(game_);
            watch_online_round();
        }
#endif
    }

    save_progress();
}

void app::shutdown() {
    audio_.shutdown();
    renderer_.shutdown();
    window_.shutdown();
}

bool app::reset_to_menu_backdrop() {
    const std::uint64_t previous_revision = game_.terrain_render_revision;
    // What the server sent stays: the menus show who is signed in.
    online_view online = std::move(game_.online);
    game_ = make_game_state(content_, game_.save);
    game_.online = std::move(online);
    if (!start_course(game_, content_.courses.front())) {
        return false;
    }
    continue_terrain_render_revision(game_, previous_revision);
    return true;
}

// Leaving a round saves like a clean exit, so nothing earned is lost.
// Online, it leaves the room but stays signed in.
void app::return_to_menu() {
    save_progress();
#if GOLFPP_NET
    if (online_ && is_online(game_)) {
        online_->leave_round();
    }
#endif
    return_to_main_menu(menu_);
    reset_to_menu_backdrop();
    audio_.stop_loop(sound_cart_drive_loop);
    cart_loop_playing_ = false;
    audio_.start_ambience(sound_ambience_menu_vcr);
}

// Only the offline save is ever written: online progress lives on the server.
void app::save_progress() {
    game_.save_requested = false;
    if (game_.play == play_mode::offline && saving_enabled_ && !write_save(save_path_, game_.save)) {
        SDL_Log("Failed to write save %s", save_path_.string().c_str());
    }
}

void app::apply_settings(const bool write) {
    audio_.set_levels(audio_levels_from_settings(menu_.settings, content_.settings));
    if (write && !write_settings(settings_path_, menu_.settings)) {
        SDL_Log("Failed to write settings %s", settings_path_.string().c_str());
    }
}

void app::update_menu(frame_profile* profile) {
    const online_menu_status online = online_status();
    const startup_menu_result result =
        update_startup_menu(menu_, input_, mouse_click_position(input_, window_.sdl_window()), catalog_, online);
    play_ui_sounds(audio_, result.sounds);
    if (result.settings_changed) {
        apply_settings(true);
    }
    carry_out(result.requests);
    if (result.action == startup_action::quit) {
        running_ = false;
    } else if (result.action == startup_action::start_course) {
        game_.play = result.play;
        if (start_course(game_, result.course)) {
            enter_playing(menu_);
            audio_.start_ambience(sound_ambience_course_day);
        } else {
            SDL_Log("Course %s failed to load", result.course.id.c_str());
            if (result.play == play_mode::online) {
                carry_out({online_request{online_request_type::leave_room, {}}});
                return_to_main_menu(menu_);
            }
        }
    }
    // Typing only reaches input_state::text_typed while a text field shows.
    set_text_input_enabled(wants_text_input(menu_));

    render_data data = make_frame(0.0f, true, profile);
    data.startup_menu = make_startup_menu_render_data(menu_, catalog_, text_, online);
    present_frame(data, profile);
}

online_menu_status app::online_status() const {
    online_menu_status status;
#if GOLFPP_NET
    if (online_) {
        status = online_->menu_status(game_, std::chrono::system_clock::now());
    }
#endif
    status.clock_seconds = static_cast<double>(SDL_GetTicks64()) / 1000.0;
    return status;
}

void app::carry_out(const std::vector<online_request>& requests) {
#if GOLFPP_NET
    if (online_) {
        online_->carry_out(requests, game_);
    }
#else
    (void)requests;
#endif
}

void app::watch_online_round() {
#if GOLFPP_NET
    if (menu_.flow != startup_flow::playing || !is_online(game_)) {
        return;
    }
    for (const reducer_failure& failure : online_->refusals()) {
        SDL_Log("The server refused %s: %s", failure.reducer.c_str(), failure.error.c_str());
    }
    const online_round_status round = online_->watch_round(game_);
    if (round.event == online_round_event::restart) {
        start_course(game_, game_.course);
    } else if (round.event == online_round_event::lost) {
        return_to_menu();
        if (round.message_key.empty()) {
            open_online_login(menu_);
        } else {
            menu_.message_key = round.message_key;
        }
    }
#endif
}

void app::update_confirm_menu(frame_profile* profile) {
    if (!menu_.confirm_active) {
        // Opened this frame; the same key press must not also answer it.
        open_confirm_menu(menu_);
    } else {
        const confirm_menu_result result =
            ::update_confirm_menu(menu_, input_, mouse_click_position(input_, window_.sdl_window()), content_.settings);
        play_ui_sounds(audio_, result.sounds);
        if (result.settings_changed) {
            apply_settings(true);
        }
        if (result.leave_round) {
            return_to_menu();
        }
    }

    const bool in_menu = menu_.flow != startup_flow::playing;
    render_data data = make_frame(0.0f, in_menu, profile);
    if (in_menu) {
        data.startup_menu = make_startup_menu_render_data(menu_, catalog_, text_);
    } else if (menu_.confirm_active) {
        data.startup_menu = make_confirm_menu_render_data(menu_, text_, content_.settings);
    }
    present_frame(data, profile);
}

void app::update_round(const float dt, frame_profile* profile) {
    {
        const profile_scope timer(profile, profile_stage::update_game);
        update_game(game_, game_input_from_keys(input_), dt, profile);
    }
    if (game_.save_requested) {
        save_progress();
    }
    play_game_audio();

    render_data data = make_frame(dt, false, profile);
    present_frame(data, profile);
}

void app::play_game_audio() {
    for (const audio_event& event : game_.audio_events) {
        audio_.play(sound_for_event(event));
    }
    game_.audio_events.clear();

    const bool cart_moving = game_.cart.active && std::abs(game_.cart.velocity) > cart_engine_idle_speed;
    if (cart_moving != cart_loop_playing_) {
        if (cart_moving) {
            audio_.play_loop(sound_cart_drive_loop);
        } else {
            audio_.stop_loop(sound_cart_drive_loop);
        }
        cart_loop_playing_ = cart_moving;
    }
}

void app::refresh_render_meshes() {
    if (render_meshes_revision_ == game_.terrain_render_revision) {
        return;
    }
    const std::uint64_t revision = game_.terrain_render_revision;
    terrain_render_mesh_ = make_terrain_render_mesh({&game_.area.ground}, revision);
    material_overlay_render_mesh_ = make_terrain_render_mesh({&game_.area.material_overlay}, revision);
    refresh_render_hole_signs(hole_signs_, game_.area, hole_sign_texts(game_), text_, revision);
    fences_ = build_render_fences(game_.area, revision);
    water_ = build_render_water(game_.area, revision);
    render_meshes_revision_ = revision;
}

render_data app::make_frame(const float dt, const bool snap_camera, frame_profile* profile) {
    {
        const profile_scope timer(profile, profile_stage::refresh_render_mesh_cache);
        refresh_static_anchor_cache(game_, profile);
        refresh_render_meshes();
    }

    const profile_scope timer(profile, profile_stage::make_render_data);
    // load_game_content refuses settings without the field of view.
    const float fov_degrees =
        static_cast<float>(setting_value(menu_.settings, *find_setting(content_.settings, setting_field_of_view)));
    render_data data = make_render_data(game_, input_, text_, content_.skills,
                                        render_meshes{&terrain_render_mesh_, &material_overlay_render_mesh_, &hole_signs_, &fences_, &water_},
                                        fov_degrees, profile);

    const camera_view desired{data.camera_position, data.camera_target, data.camera_fov_degrees};
    const camera_rig rig = active_camera_rig(game_);
    const camera_transition_settings settings{game_.tuning.camera.transition_seconds, game_.tuning.camera.transition_jump_distance};
    camera_transition_ = snap_camera ? snap_camera_transition(rig, desired)
                                     : update_camera_transition(camera_transition_, rig, desired, dt, settings);
    data.camera_position = camera_transition_.shown.position;
    data.camera_target = camera_transition_.shown.target;
    data.camera_fov_degrees = camera_transition_.shown.fov_degrees;
    data.camera_underwater = under_water(game_.area, data.camera_position);
    return data;
}

void app::present_frame(render_data& data, frame_profile* profile) {
    data.show_fps = profiler_.enabled;
    if (data.show_fps) {
        const float frame_ms = profiler_.published.frame_ms;
        const int fps = frame_ms > 0.0f ? static_cast<int>(std::lround(1000.0f / frame_ms)) : 0;
        data.fps_label = format_text(text_, text_hud_fps,
                                     {{"fps", std::to_string(fps)}, {"ms", std::to_string(static_cast<int>(std::lround(frame_ms)))}});
        data.profile_summary = profiler_.published;
    }

    renderer_.render(data, text_, profile);
    if (profile != nullptr) {
        const renderer_cull_stats& cull = renderer_.cull_stats();
        profile->visible_chunks = cull.terrain.chunks_visible + cull.material_overlay.chunks_visible;
        profile->culled_chunks = cull.terrain.chunks_culled + cull.material_overlay.chunks_culled;
        profile->chunk_draw_ranges = cull.terrain.draw_ranges + cull.material_overlay.draw_ranges;
        profile->chunk_indices_drawn = cull.terrain.indices_drawn + cull.material_overlay.indices_drawn;
        profile->chunk_indices_total = cull.terrain.indices_total + cull.material_overlay.indices_total;
        profile->trees_visible = cull.trees_visible;
    }
    const profile_scope timer(profile, profile_stage::window_swap);
    window_.swap();
}
