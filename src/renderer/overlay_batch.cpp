#include "renderer/overlay_batch.h"

#include <cmath>

#include <glm/geometric.hpp>

namespace {
void append_transformed_quad(overlay_batch& batch,
                             const glm::vec2 center,
                             const glm::vec2 column_x,
                             const glm::vec2 column_y,
                             const glm::vec4 color) {
    // Mirrors mat4 * vec4(ux, uy, 0, 1) for a 2D affine model whose columns
    // are column_x, column_y and the translation `center`, grouped the way
    // glm evaluates it so results match the old matrix path.
    for (const std::array<float, 2>& corner : overlay_unit_quad_corners) {
        overlay_vertex vertex;
        vertex.position = (column_x * corner[0] + column_y * corner[1]) + center;
        vertex.color = color;
        batch.vertices.push_back(vertex);
    }
}
}

void clear_overlay_batch(overlay_batch& batch) {
    batch.vertices.clear();
}

std::size_t overlay_batch_quad_count(const overlay_batch& batch) {
    return batch.vertices.size() / overlay_vertices_per_quad;
}

void draw_overlay_quad(overlay_batch& batch,
                       const glm::vec2 center,
                       const glm::vec2 half_size,
                       const glm::vec3 color,
                       const float alpha) {
    append_transformed_quad(batch,
                            center,
                            glm::vec2(half_size.x, 0.0f),
                            glm::vec2(0.0f, half_size.y),
                            glm::vec4(color, alpha));
}

void draw_overlay_rotated_quad(overlay_batch& batch,
                               const glm::vec2 center,
                               const glm::vec2 half_size,
                               const float angle_radians,
                               const glm::vec3 color,
                               const float alpha) {
    // glm::rotate about +Z yields columns (c, s) and (-s, c); glm::scale then
    // multiplies those columns by half_size.
    const float c = std::cos(angle_radians);
    const float s = std::sin(angle_radians);
    append_transformed_quad(batch,
                            center,
                            glm::vec2(c, s) * half_size.x,
                            glm::vec2(-s, c) * half_size.y,
                            glm::vec4(color, alpha));
}

void draw_overlay_segment(overlay_batch& batch,
                          const glm::vec2 start,
                          const glm::vec2 end,
                          const float thickness,
                          const glm::vec3 color,
                          const float alpha) {
    const glm::vec2 delta = end - start;
    const float length = glm::length(delta);
    if (length <= 0.00001f) {
        return;
    }

    draw_overlay_rotated_quad(batch,
                              (start + end) * 0.5f,
                              glm::vec2(length * 0.5f, thickness * 0.5f),
                              std::atan2(delta.y, delta.x),
                              color,
                              alpha);
}
