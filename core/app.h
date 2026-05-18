#pragma once

#include "core/input.h"
#include "core/window.h"
#include "game/game_state.h"
#include "renderer/renderer.h"

struct app {
    bool init();
    void run();
    void shutdown();

private:
    window window_;
    renderer renderer_;
    input_state input_;
    game_state game_;
    bool running_ = false;
};
