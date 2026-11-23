#include "renderer/picture_size.h"

#include <algorithm>

picture_size picture_size_for_window(const int window_width, const int window_height, const int scene_lines,
                                     const int ui_lines) {
    const int width = std::max(1, window_width);
    const int height = std::max(1, window_height);
    const int lines = std::max(1, scene_lines);
    const int ui = std::max(1, ui_lines);
    picture_size size;
    size.factor = std::max(1, (height + lines / 2) / lines);
    const int ui_window_rows = size.factor * ui;
    size.unit = std::max(1, height / ui_window_rows);
    const int cell = size.factor * size.unit;
    size.ui_width = (width + cell - 1) / cell;
    size.ui_height = (height + cell - 1) / cell;
    size.width = size.ui_width * size.unit;
    size.height = size.ui_height * size.unit;
    size.crop_x = (size.width * size.factor - width) / 2;
    size.crop_y = (size.height * size.factor - height) / 2;
    return size;
}
