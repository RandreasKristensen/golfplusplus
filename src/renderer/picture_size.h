#pragma once

// How big the scene target is for a window, and how the TV pass lays it on
// the window: every scene pixel is exactly `factor` x `factor` window pixels,
// so lines stay even and pixels square at any window size. GL-free.

struct picture_size {
    int width = 1;   // the scene target, in scene pixels
    int height = 1;
    int factor = 1;  // window pixels per scene pixel, both ways
    // Window pixels cut off the left and bottom: the scene overhangs the
    // window by less than one scene pixel, split evenly between the sides.
    int crop_x = 0;
    int crop_y = 0;
};

// factor = max(1, round(window_height / target_lines)); the scene is
// ceil(window / factor) both ways.
picture_size picture_size_for_window(int window_width, int window_height, int target_lines);
