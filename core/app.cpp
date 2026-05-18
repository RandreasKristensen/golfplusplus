#include "core/app.h"

#include "core/event_loop.h"

#include <SDL.h>

#include <algorithm>

namespace {
render_data make_render_data(const game_state& game) {
    render_data data;
    data.ball_position = game.ball.position;
    data.tee_position = game.tuning.course.tee_position;
    data.pin_position = game.tuning.course.pin_position;
    data.cup_radius = game.tuning.course.cup_radius;
    data.course_extent = game.tuning.course.extent;
    data.aim_angle = game.aim_angle;
    data.ball_moving = ball_is_moving(game.ball, game.tuning);
    data.swing_timing = game.swing.phase == swing_phase::timing;
    data.swing_power = game.swing.power;
    data.stroke_count = game.stroke_count;
    data.selected_club = static_cast<int>(game.selected_club);
    return data;
}
}

bool app::init() {
    if (!window_.init("vcr-golf", 1280, 720)) {
        return false;
    }

    if (!renderer_.init(window_.sdl_window())) {
        window_.shutdown();
        return false;
    }

    game_ = make_initial_game_state();
    running_ = true;
    return true;
}

void app::run() {
    Uint64 previous_counter = SDL_GetPerformanceCounter();
    const double performance_frequency = static_cast<double>(SDL_GetPerformanceFrequency());

    while (running_) {
        const Uint64 current_counter = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(current_counter - previous_counter) / performance_frequency;
        previous_counter = current_counter;
        const float dt = std::min(static_cast<float>(elapsed), 0.05f);

        input_.reset_frame();
        poll_events(input_);

        if (input_.quit_requested) {
            running_ = false;
        }

        update_game(game_, input_, dt);
        renderer_.render(make_render_data(game_));
        window_.swap();
    }
}

void app::shutdown() {
    renderer_.shutdown();
    window_.shutdown();
}
