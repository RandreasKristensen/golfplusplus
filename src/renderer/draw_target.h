#pragma once

// The framebuffer a view is drawn into and the state the renderer set on it.
// Passes take it as an argument instead of reading GL state back, which blocks
// the app thread until the driver has drained its queue. Front faces are
// counter-clockwise outside a mirror.

struct draw_target {
    unsigned int framebuffer = 0;
    int width = 0;
    int height = 0;
    bool scissored = false;  // the scissor test is enabled while this view draws
};
