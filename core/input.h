#pragma once

struct input_state {
    bool quit_requested = false;

    void reset_frame() {
        quit_requested = false;
    }
};
