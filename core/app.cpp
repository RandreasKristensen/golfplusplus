#include "core/app.h"

#include "core/event_loop.h"

#include <SDL.h>

#include <algorithm>

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
        renderer_.render(game_.ball.position);
        window_.swap();
    }
}

void app::shutdown() {
    renderer_.shutdown();
    window_.shutdown();
}
