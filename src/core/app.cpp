#include "core/app.h"

#include "audio/audio_manifest.h"
#include "audio/sound_ids.h"
#include "core/event_loop.h"
#include "core/god_menu.h"
#include "core/key_bindings.h"
#include "core/render_frame.h"
#include "core/screenshot.h"
#include "core/sky_frame.h"
#include "core/tape_tracking.h"
#include "core/vcr_frame.h"
#include "game/asset_resolver.h"
#include "game/content_files.h"
#include "game/course_session.h"
#include "game/golf_cart.h"
#include "game/group_round.h"
#include "game/lighting_loader.h"
#include "game/mode_dispatch.h"
#include "game/model_kinds.h"
#include "game/net_types.h"
#include "game/picture_loader.h"
#include "game/save_manager.h"
#include "game/settings.h"
#include "game/text_ids.h"
#include "game/world_sounds.h"
#include "renderer/sky_visibility.h"
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
// GOLFPP_SCREENSHOT is taken on this frame after loading, once the camera and
// streaming have settled.
constexpr int screenshot_frame = 120;
// render_data::clock_seconds wraps at this, so a float keeps sub-millisecond steps.
constexpr double clock_wrap_seconds = 3600.0;

std::int64_t unix_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string sdl_path(char* path) {
    const std::string result = path != nullptr ? path : "";
    SDL_free(path);
    return result;
}

// The mouse this frame in overlay clip space (-1..1, y up).
menu_pointer mouse_menu_pointer(const input_state& input, SDL_Window* window) {
    if (window == nullptr) {
        return {};
    }
    int width = 1;
    int height = 1;
    SDL_GetWindowSize(window, &width, &height);
    const float x = static_cast<float>(input.mouse_x) / static_cast<float>(std::max(1, width)) * 2.0f - 1.0f;
    const float y = 1.0f - static_cast<float>(input.mouse_y) / static_cast<float>(std::max(1, height)) * 2.0f;
    menu_pointer pointer;
    if (input.mouse_dx != 0 || input.mouse_dy != 0) {
        pointer.hover = glm::vec2(x, y);
    }
    if (input.mouse_left.pressed) {
        pointer.click = glm::vec2(x, y);
    }
    return pointer;
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
    case audio_event_type::item_use:
        return event.sound;
    case audio_event_type::ball_land:
        if (event.material == terrain_material::water) {
            return sound_ball_splash;
        }
        return event.material == terrain_material::bunker ? sound_ball_land_bunker : sound_ball_land_grass;
    case audio_event_type::ball_hit:
        switch (event.hit) {
        case shot_event_kind::pole_hit:
            return sound_ball_pole_hit;
        case shot_event_kind::board_hit:
            return sound_ball_board_hit;
        case shot_event_kind::flag_hit:
            return sound_ball_flag_hit;
        case shot_event_kind::cup_rattle:
            return sound_ball_cup_rattle;
        case shot_event_kind::leaves:
            return sound_ball_leaves;
        case shot_event_kind::person_hit:
            return sound_ball_person_hit;
        case shot_event_kind::ball_hit:
            return sound_ball_ball_hit;
        case shot_event_kind::land:
        case shot_event_kind::tree_hit:
            break;
        }
        return sound_ball_tree_hit;
    case audio_event_type::ball_cup:
        return sound_ball_cup;
    case audio_event_type::club_change:
        return sound_club_change;
    case audio_event_type::bag_moved:
        return sound_bag_move;
    case audio_event_type::cart_start:
        return sound_cart_start;
    case audio_event_type::cart_crash:
        return sound_cart_crash;
    case audio_event_type::cart_tip:
        return sound_cart_tip;
    case audio_event_type::cart_sink:
        return sound_cart_sink;
    case audio_event_type::cart_righted:
        return sound_cart_righted;
    case audio_event_type::player_bump:
        return sound_player_bump;
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
    const bool sized = options.window_width > 0 && options.window_height > 0;
    if (!window_.init("golf++", sized ? options.window_width : window_width, sized ? options.window_height : window_height,
                      options.vsync, asset_root + "/interface/icons/golfpp-icon.bmp")) {
        return false;
    }
    const std::optional<picture_style> picture = load_picture_style(asset_root);
    if (!picture) {
        SDL_Log("Failed to load %s from %s", picture_style_path, asset_root.c_str());
        return false;
    }
    std::optional<lighting_palette> lighting = load_lighting(asset_root);
    if (!lighting) {
        SDL_Log("Failed to load %s from %s", lighting_path, asset_root.c_str());
        return false;
    }
    lighting_ = std::move(*lighting);
    prop_catalog_load_result props = load_props(asset_root);
    if (!props.props) {
        SDL_Log("Failed to load props: %s from %s", props.error.c_str(), asset_root.c_str());
        return false;
    }
    props_ = std::move(*props.props);
    sky_.pinned_hour = options.game_hour;
    sky_.pinned_date = options.game_date;
    god_.enabled = options.god_menu;
    god_.weather = options.weather;
    god_.wind_speed = options.wind_speed;
    god_.wind_heading = options.wind_speed ? std::optional<float>(0.0f) : std::nullopt;
    god_.wetness = options.wetness;
    screenshot_path_ = options.screenshot_path;
    tape_style_ = picture->tape;
    if (!renderer_.init(window_.sdl_window(), asset_root, *picture, lighting_, props_)) {
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
    // A GOLFPP_SCREENSHOT run is reproducible and leaves the player's files
    // alone: default settings, and the save is read but never written.
    const bool review_run = !screenshot_path_.empty();
    menu_.settings = review_run ? settings_values{} : load_settings(settings_path_, content_.settings);
    const save_load_result save = load_save(save_path_);
    saving_enabled_ = !review_run;
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
    audio_.load_manifest(std::filesystem::path(asset_root) / sounds_path);
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
            ctrl_wheeled_ = false;
        }
        ctrl_wheeled_ = ctrl_wheeled_ || (input_.ctrl.is_down && input_.wheel_steps != 0);
        if (input_.ctrl.released && !ctrl_wheeled_) {
            profiler_.enabled = !profiler_.enabled;
        }
        sky_ = scrub_sky(sky_, profiler_.enabled && input_.key_t.is_down, dt);
        game_.local_unix_ms = unix_now_ms();
        run_god_menu(god_, sky_, game_, god_menu_keys_of(input_));
        profiler_end_frame(profiler_, dt);
        profiler_begin_frame(profiler_);
        frame_profile* profile = profiler_frame(profiler_);

        if (menu_.flow != startup_flow::playing) {
            update_menu(profile);
        } else if (round_results_shown(game_)) {
            set_mouse_captured(false);
            // Online the server has started the next round already; offline
            // the round ends at the menu.
            const bool next = input_.enter.pressed || input_.space.pressed;
            if (next && is_online(game_)) {
                audio_.play(sound_ui_select);
                start_next_round(game_);
            } else if (next || input_.escape.pressed) {
                audio_.play(sound_ui_select);
                return_to_menu();
            }
            render_data data = make_frame(dt, menu_.flow != startup_flow::playing, profile);
            present_frame(data, profile);
        } else if (menu_.confirm_active ||
                   (input_.escape.pressed && game_.mode == game_mode::walking && !game_.give_up_prompt && !game_.picker)) {
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
    stop_motor_loops();
    audio_.start_ambience(sound_ambience_menu_vcr);
}

// The settings screen shows the golfer's own settings as they are kept with progress.
void app::show_golfer_settings() {
    menu_.settings[setting_natural_shape] = natural_shape_index(my_golfer(game_).natural);
}

// Only the offline save is ever written: online progress lives on the server.
void app::save_progress() {
    game_.save_requested = false;
    if (game_.play == play_mode::offline && saving_enabled_ && !write_save(save_path_, game_.save)) {
        SDL_Log("Failed to write save %s", save_path_.string().c_str());
    }
}

void app::apply_settings(const bool write) {
    const natural_shape natural = *natural_shape_from_index(
        setting_value(menu_.settings, *find_setting(content_.settings, setting_natural_shape)));
    if (write && natural != my_golfer(game_).natural) {
        record_natural_shape(game_, natural);
    }
    audio_.set_levels(audio_levels_from_settings(menu_.settings, content_.settings));
    // load_game_content refuses settings without fullscreen.
    window_.set_fullscreen(setting_value(menu_.settings, *find_setting(content_.settings, setting_fullscreen)) != 0);
    renderer_.set_tape_wear(setting_fraction(menu_.settings, *find_setting(content_.settings, setting_tape_wear)));
    if (write && !write_settings(settings_path_, menu_.settings, content_.settings)) {
        SDL_Log("Failed to write settings %s", settings_path_.string().c_str());
    }
}

void app::update_menu(frame_profile* profile) {
    set_mouse_captured(false);
    const online_menu_status online = online_status();
    show_golfer_settings();
    const startup_menu_result result =
        update_startup_menu(menu_, input_, mouse_menu_pointer(input_, window_.sdl_window()), catalog_, online);
    play_ui_sounds(audio_, result.sounds);
    if (result.settings_changed) {
        apply_settings(true);
    }
    carry_out(result.requests);
    if (result.action == startup_action::start_course) {
        game_.play = result.play;
    }
    if (result.natural) {
        record_natural_shape(game_, *result.natural);
        save_progress();
    }
    if (result.action == startup_action::quit) {
        running_ = false;
    } else if (result.action == startup_action::start_course && !natural_shape_picked(game_)) {
        // A golfer's first round starts once they have picked a natural shot.
        open_natural_shape_picker(menu_, result.course, result.play);
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
    set_mouse_captured(false);
    if (!menu_.confirm_active) {
        // Opened this frame; the same key press must not also answer it.
        open_confirm_menu(menu_, ball_in_play(game_) ? std::optional<std::size_t>(game_.ball->hole) : std::nullopt);
    } else {
        show_golfer_settings();
        const confirm_menu_result result =
            ::update_confirm_menu(menu_, input_, mouse_menu_pointer(input_, window_.sdl_window()), catalog_);
        play_ui_sounds(audio_, result.sounds);
        if (result.settings_changed) {
            apply_settings(true);
        }
        if (result.leave_round) {
            return_to_menu();
        }
        if (result.give_up_hole) {
            give_up_hole(game_);
        }
    }
    set_text_input_enabled(wants_text_input(menu_));

    const bool in_menu = menu_.flow != startup_flow::playing;
    render_data data = make_frame(0.0f, in_menu, profile);
    if (in_menu) {
        data.startup_menu = make_startup_menu_render_data(menu_, catalog_, text_);
    } else if (menu_.confirm_active) {
        data.startup_menu = make_confirm_menu_render_data(menu_, text_, content_.settings);
    }
    present_frame(data, profile);
}

// While a round plays the mouse looks around; the menus, the pause menu and
// the results give the cursor back.
float app::mouse_look_radians() const {
    // Counts from before the capture moved a cursor, not the look.
    if (SDL_GetRelativeMouseMode() != SDL_TRUE) {
        return 0.0f;
    }
    // load_game_content refuses settings without the mouse sensitivity.
    const float percent =
        static_cast<float>(setting_value(menu_.settings, *find_setting(content_.settings, setting_mouse_sensitivity)));
    return game_.tuning.player.mouse_look_radians * percent / 100.0f;
}

void app::update_round(const float dt, frame_profile* profile) {
    {
        const profile_scope timer(profile, profile_stage::update_game);
        game_.hand = setting_value(menu_.settings, *find_setting(content_.settings, setting_handedness)) != 0
            ? handedness::left
            : handedness::right;
        update_game(game_,
                    game_input_from_keys(god_.open ? without_god_menu_keys(input_) : input_, mouse_look_radians(),
                                         game_.tuning.setup.drag_threshold_counts),
                    dt, profile);
    }
    set_mouse_captured(true);
    set_text_input_enabled(false);
    if (game_.save_requested) {
        save_progress();
    }

    render_data data = make_frame(dt, false, profile);
    play_game_audio();
    present_frame(data, profile);
}

void app::play_game_audio() {
    audio_.set_listener(listener_looking(camera_transition_.shown.position, camera_transition_.shown.target),
                        game_.tuning.scale.meters_per_world_unit);
    for (const audio_event& event : game_.audio_events) {
        audio_.play_at(sound_for_event(event), event.position);
    }
    game_.audio_events.clear();

    std::vector<std::string> running;
    for (const running_motor& motor : running_motors(game_)) {
        const std::string key = "cart_motor:" + std::to_string(motor.owner);
        audio_.play_loop(key, sound_cart_drive_loop, motor.position);
        audio_.place_loop(key, motor.position);
        audio_.set_loop_pitch(key, motor.pitch);
        running.push_back(key);
    }
    for (const std::string& key : motor_loops_) {
        if (std::find(running.begin(), running.end(), key) == running.end()) {
            audio_.stop_loop(key);
        }
    }
    motor_loops_ = std::move(running);
}

void app::stop_motor_loops() {
    for (const std::string& key : motor_loops_) {
        audio_.stop_loop(key);
    }
    motor_loops_.clear();
}

void app::refresh_render_meshes() {
    if (render_meshes_revision_ == game_.terrain_render_revision) {
        return;
    }
    const std::uint64_t revision = game_.terrain_render_revision;
    const scene_colors& colors = lighting_.colors;
    const ground_light_bake light = bake_ground_light(game_.area.ground);
    terrain_render_mesh_ = make_terrain_render_mesh({&game_.area.ground}, revision, colors.ground, light);
    material_overlay_render_mesh_ =
        make_terrain_render_mesh({&game_.area.material_overlay}, revision, colors.ground, light);
    refresh_render_hole_signs(hole_signs_, game_.area, hole_sign_texts(game_), text_, colors, props_.at(hole_sign_model), revision);
    fences_ = build_render_fences(game_.area, revision, props_.at(fence_model).color("post"), game_.tuning.fence.pole_radius);
    water_ = build_render_water(game_.area, revision);
    floodlights_ = build_render_floodlights(game_.area, revision, props_.at(floodlight_model));
    render_meshes_revision_ = revision;
}

render_data app::make_frame(const float dt, const bool snap_camera, frame_profile* profile) {
    {
        const profile_scope timer(profile, profile_stage::refresh_render_mesh_cache);
        refresh_static_anchor_cache(game_, profile);
        refresh_render_meshes();
        refresh_tree_shadows(tree_shadows_, game_, lighting_.shadows);
    }

    const profile_scope timer(profile, profile_stage::make_render_data);
    // load_game_content refuses settings without the field of view.
    const float fov_degrees =
        static_cast<float>(setting_value(menu_.settings, *find_setting(content_.settings, setting_field_of_view)));
    const bool interact_prompt = setting_value(menu_.settings, *find_setting(content_.settings, setting_interact_prompt)) != 0;
    render_data data = make_render_data(game_, text_, content_.skills,
                                        render_meshes{&terrain_render_mesh_, &material_overlay_render_mesh_, &hole_signs_, &fences_, &water_, &floodlights_},
                                        props_, fov_degrees, interact_prompt);

    const camera_view desired{data.camera_position, data.camera_target, data.camera_fov_degrees};
    const camera_rig rig = active_camera_rig(game_);
    const camera_transition_settings settings{game_.tuning.camera.transition_seconds, game_.tuning.camera.transition_jump_distance};
    camera_transition_ = snap_camera ? snap_camera_transition(rig, desired)
                                     : update_camera_transition(camera_transition_, rig, desired, dt, settings);
    data.camera_position = camera_transition_.shown.position;
    data.camera_target = camera_transition_.shown.target;
    data.camera_fov_degrees = camera_transition_.shown.fov_degrees;
    data.camera_underwater = under_water(game_.area, data.camera_position);
    weather_shown_ = update_frame_weather(weather_shown_, game_, lighting_, data, menu_.flow == startup_flow::playing, dt);
    for (int i = 0; i < weather_shown_.lightning.close_claps + weather_shown_.lightning.far_claps; ++i) {
        audio_.play(i < weather_shown_.lightning.close_claps ? sound_thunder_close : sound_thunder_far);
    }
    data.lighting = make_render_lighting(game_, lighting_, sky_, unix_now_ms(), weather_shown_.shown);
    data.weather = make_frame_weather(weather_shown_, game_, lighting_, data);
    add_frame_lights(data, game_, lighting_, tree_shadows_);
    if (god_.open && menu_.flow == startup_flow::playing) {
        data.god_menu = god_menu_frame_view(god_, sky_, game_, text_);
    }
    data.solid_outlines = god_solid_outlines(god_, game_, data.camera_position);
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

    const std::uint64_t ticks = SDL_GetTicks64();
    const float dt = last_present_ticks_ != 0 ? static_cast<float>(ticks - last_present_ticks_) / 1000.0f : 0.0f;
    last_present_ticks_ = ticks;
    const bool paused = menu_.flow == startup_flow::playing && (menu_.confirm_active || round_results_shown(game_));
    tape_tracking_ = update_tape_tracking(tape_tracking_, tape_tracking_signals_for(game_, paused), dt, tape_style_);
    const vcr_signals vcr{menu_.flow != startup_flow::playing, paused,
                          setting_value(menu_.settings, *find_setting(content_.settings, setting_datestamp)) != 0};
    data.vcr = make_render_vcr(text_, game_, sky_, unix_now_ms(), vcr);
    data.clock_seconds = static_cast<float>(std::fmod(static_cast<double>(ticks) / 1000.0, clock_wrap_seconds));
    data.tape_tracking = tape_tracking_.level;

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
    if (!screenshot_path_.empty() && ++frames_presented_ == screenshot_frame) {
        if (!save_screenshot(window_.sdl_window(), screenshot_path_)) {
            SDL_Log("GOLFPP_SCREENSHOT: cannot write %s", screenshot_path_.string().c_str());
        }
        running_ = false;
    }
    const profile_scope timer(profile, profile_stage::window_swap);
    window_.swap();
}
