#pragma once

#include "core/input.h"
#include "core/window.h"

struct app {
    bool init();
    void run();
    void shutdown();

private:
    window window_;
    input_state input_;
    bool running_ = false;
};
