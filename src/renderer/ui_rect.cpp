#include "renderer/ui_rect.h"

#include <algorithm>

#include <glm/common.hpp>

float rect_left(const ui_rect& rect) {
    return rect.center.x - rect.half_size.x;
}

float rect_right(const ui_rect& rect) {
    return rect.center.x + rect.half_size.x;
}

float rect_top(const ui_rect& rect) {
    return rect.center.y + rect.half_size.y;
}

float rect_bottom(const ui_rect& rect) {
    return rect.center.y - rect.half_size.y;
}

ui_rect rect_from_edges(const float left, const float bottom, const float right, const float top) {
    return ui_rect{glm::vec2((left + right) * 0.5f, (bottom + top) * 0.5f),
                   glm::vec2(std::max(0.0f, right - left) * 0.5f, std::max(0.0f, top - bottom) * 0.5f)};
}

ui_rect inset_rect(const ui_rect& rect, const glm::vec2 amount) {
    return ui_rect{rect.center, glm::max(rect.half_size - amount, glm::vec2(0.0f))};
}

ui_rect slice_x(const ui_rect& rect, const float from, const float to) {
    const float width = rect.half_size.x * 2.0f;
    return rect_from_edges(rect_left(rect) + width * from, rect_bottom(rect), rect_left(rect) + width * to, rect_top(rect));
}

ui_rect slice_y(const ui_rect& rect, const float from, const float to) {
    const float height = rect.half_size.y * 2.0f;
    return rect_from_edges(rect_left(rect), rect_top(rect) - height * to, rect_right(rect), rect_top(rect) - height * from);
}

ui_rect rect_row(const ui_rect& rect, const int index, const int count) {
    const float rows = static_cast<float>(std::max(1, count));
    return slice_y(rect, static_cast<float>(index) / rows, static_cast<float>(index + 1) / rows);
}
