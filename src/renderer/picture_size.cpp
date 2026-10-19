#include "renderer/picture_size.h"

#include <algorithm>

picture_size picture_size_for_window(const int window_width, const int window_height, const int target_lines) {
    const int width = std::max(1, window_width);
    const int height = std::max(1, window_height);
    const int lines = std::max(1, target_lines);
    picture_size size;
    size.factor = std::max(1, (height + lines / 2) / lines);
    size.width = (width + size.factor - 1) / size.factor;
    size.height = (height + size.factor - 1) / size.factor;
    size.crop_x = (size.width * size.factor - width) / 2;
    size.crop_y = (size.height * size.factor - height) / 2;
    return size;
}
