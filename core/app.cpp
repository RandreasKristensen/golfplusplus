#include "core/app.h"

#include "core/event_loop.h"

bool app::init() {
    if (!window_.init("vcr-golf", 1280, 720)) {
        return false;
    }

    if (!renderer_.init(window_.sdl_window())) {
        window_.shutdown();
        return false;
    }

    running_ = true;
    return true;
}

void app::run() {
    while (running_) {
        input_.reset_frame();
        poll_events(input_);

        if (input_.quit_requested) {
            running_ = false;
        }

        renderer_.render();
        window_.swap();
    }
}

void app::shutdown() {
    renderer_.shutdown();
    window_.shutdown();
}
