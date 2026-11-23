// The normal app loop and delivery of its completed frame records.
#include "core/app.h"
#include "core/benchmark_driver.h"
#include "audio/sound_ids.h"
#include "core/event_loop.h"
#include "core/key_bindings.h"
#include "core/play_screen.h"
#include "game/settings.h"
#include <chrono>

std::int64_t app::unix_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void app::advance_game(const game_input& input, const float dt, frame_profile* profile) {
    const profile_scope timer(profile, profile_stage::update_game);
    if (benchmark_) { benchmark_->launch_if_due(game_); }
    update_game(game_, input, dt, profile);
}

void app::update_round(const float dt, frame_profile* profile) {
    game_.hand = setting_value(menu_.settings, *find_setting(content_.settings, setting_handedness)) != 0
        ? handedness::left
        : handedness::right;
    game_.favourite_colour = static_cast<std::uint8_t>(
        setting_value(menu_.settings, *find_setting(content_.settings, setting_favourite_colour)));
    advance_game(game_input_from_keys(god_.open ? without_god_menu_keys(input_) : input_, mouse_look_radians(),
                                      game_.tuning.setup), dt, profile);
    set_mouse_captured(!benchmark_);
    set_text_input_enabled(false);
    if (game_.save_requested) {
        save_progress();
    }

    render_data data = make_frame(dt, false, profile, benchmark_ ? benchmark_->camera() : std::nullopt);
    play_game_audio();
    present_frame(data, profile);
}

void app::run(const frame_profile_sink& sink) {
    const frame_profile_sink publish = [&](const frame_profile& frame) {
        publish_profile(profiler_, frame);
        if (sink) { sink(frame); }
    };
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double counter_frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running_) {
        if (benchmark_ && !benchmark_->prepare(game_, sky_, god_, profiler_.next_frame)) { break; }
        if (benchmark_ && benchmark_->entering()) { weather_shown_ = {}; camera_transition_ = {}; }
        const Uint64 counter = SDL_GetPerformanceCounter();
        const float dt = static_cast<float>(static_cast<double>(counter - previous_counter) / counter_frequency);
        previous_counter = counter;

        profiler_.enabled = god_.show_profiler || static_cast<bool>(sink);
        frame_profile* profile = profiler_begin_frame(profiler_);
        renderer_.begin_profile(profile);
        input_ = poll_events(input_);
        if (input_.quit_requested) {
            running_ = false;
        }
        if (benchmark_) { input_ = benchmark_->keys(); }
        game_.local_unix_ms = unix_now_ms();
        update_look(dt);
        run_god_menu(god_, sky_, game_, god_menu_keys_of(look_tuning_.open ? without_god_menu_keys(input_) : input_));
        sky_ = scrub_sky(sky_, god_.show_profiler && input_.key_t.is_down, dt);

        if (menu_.flow == startup_flow::touring) {
            update_tour(dt, profile);
        } else if (menu_.flow != startup_flow::playing) {
            update_menu(dt, profile);
        } else if (const play_frame frame = play_frame_for(game_, menu_.confirm_active, input_.escape.pressed);
                   frame.screen == play_screen::hub_menu) {
            update_hub_menu(dt, profile);
        } else if (frame.screen == play_screen::results) {
            set_mouse_captured(false);
            if (input_.enter.pressed || input_.space.pressed || input_.escape.pressed) {
                audio_.play(sound_ui_select);
                game_.results_dismissed = true;
            }
            if (frame.world_updates) {
                advance_game(game_input{}, dt, profile);
                play_game_audio();
            }
            render_data data = make_frame(dt, menu_.flow != startup_flow::playing, profile);
            present_frame(data, profile);
        } else if (frame.screen == play_screen::pause) {
            if (frame.world_updates) {
                advance_game(game_input{}, dt, profile);
            }
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
        for (const sync_breach& breach : game_.motion_sync.reports) {
            SDL_Log("Sync breach %s: predicted (%.4f, %.4f, %.4f), row (%.4f, %.4f, %.4f), ack %.9f, refusal %s",
                    breach.thing.c_str(), breach.predicted.x, breach.predicted.y, breach.predicted.z,
                    breach.authoritative.x, breach.authoritative.y, breach.authoritative.z,
                    breach.ack, breach.refusal.c_str());
        }
        game_.motion_sync.reports.clear();
        if (profile) {
            profile->frame_ms = static_cast<float>((SDL_GetPerformanceCounter() - counter) * 1000.0 / counter_frequency);
            profile->memory = sample_process_memory();
            profile->course_load_ms = course_load_ms_;
            profile->course_bake_ms = course_bake_ms_;
            if (benchmark_) { benchmark_->advance(profile->frame_ms); }
            renderer_.finish_profile(publish);
        }
    }

    renderer_.flush_profiles(publish);
    save_progress();
}
