#pragma once

// Axis-aligned boxes in overlay clip space, and the helpers that cut a panel
// into the boxes its parts are drawn in. All overlay text is drawn into one of
// these (renderer/pixel_font.h): layout code says where text may go, the text
// layout decides how big it can be. GL-free.

#include <glm/vec2.hpp>

struct ui_rect {
    glm::vec2 center = glm::vec2(0.0f);
    glm::vec2 half_size = glm::vec2(0.0f);
};

float rect_left(const ui_rect& rect);
float rect_right(const ui_rect& rect);
float rect_top(const ui_rect& rect);
float rect_bottom(const ui_rect& rect);

ui_rect rect_from_edges(float left, float bottom, float right, float top);
// Shrunk by `amount` on every side, never below zero size.
ui_rect inset_rect(const ui_rect& rect, glm::vec2 amount);
// The part between fractions `from` and `to` of the width, measured from the left.
ui_rect slice_x(const ui_rect& rect, float from, float to);
// The part between fractions `from` and `to` of the height, measured from the top.
ui_rect slice_y(const ui_rect& rect, float from, float to);
// Row `index` of `count` equal rows, top to bottom.
ui_rect rect_row(const ui_rect& rect, int index, int count);
