#include "core/app.h"

#include "core/event_loop.h"

bool app::init() {
    if (!window_.init("vcr-golf", 1280, 720)) {
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

        // TODO: update and render once systems exist.
        window_.swap();
    }
}

void app::shutdown() {
    window_.shutdown();
}
