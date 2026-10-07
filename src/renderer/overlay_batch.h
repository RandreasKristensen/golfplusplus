#pragma once

// CPU-side builder for the batched 2D overlay (HUD, menus, pixel text).
//
// Every overlay primitive appends six vertices (two triangles) already in
// overlay clip space, each carrying its own RGBA. Because colour and alpha are
// per vertex, submission order is drawing order (painter's algorithm) and the
// whole stream goes out in a single draw call.
//
// "draw_overlay_*" here means "append to the batch" — nothing touches GL. The
// GL upload/draw lives in renderer/overlay_pass.h. Kept GL-free so the vertex
// math can be unit tested.

#include <array>
#include <cstddef>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

struct overlay_vertex {
    glm::vec2 position = glm::vec2(0.0f);  // overlay clip space (z = 0, w = 1)
    glm::vec4 color = glm::vec4(0.0f);     // rgb + alpha, not premultiplied
};

inline constexpr std::size_t overlay_vertices_per_quad = 6;

// Size in pixels of the low-res target the overlay is drawn into. Text is
// laid out on this grid so every font pixel is a whole block of target pixels.
struct overlay_grid {
    int width = 0;
    int height = 0;
};

struct overlay_batch {
    // Reused across frames: clear() keeps capacity so steady-state frames do
    // not allocate.
    std::vector<overlay_vertex> vertices;
    overlay_grid grid;
    // Labels drawn this batch that did not fit their box and were cut off
    // with an ellipsis. Layout tests require zero for shipped text.
    std::size_t truncated_text_count = 0;
};

// Unit-quad corners (x, y), two triangles.
inline constexpr std::array<std::array<float, 2>, overlay_vertices_per_quad> overlay_unit_quad_corners{{
    {{-1.0f, -1.0f}},
    {{ 1.0f, -1.0f}},
    {{ 1.0f,  1.0f}},
    {{-1.0f, -1.0f}},
    {{ 1.0f,  1.0f}},
    {{-1.0f,  1.0f}}
}};

// Empties the vertices and the truncation count; keeps the grid and capacity.
void clear_overlay_batch(overlay_batch& batch);
std::size_t overlay_batch_quad_count(const overlay_batch& batch);

// Axis-aligned quad: corners are center +/- half_size.
void draw_overlay_quad(overlay_batch& batch,
                       glm::vec2 center,
                       glm::vec2 half_size,
                       glm::vec3 color,
                       float alpha = 1.0f);

// Rotated quad: translate(center) * rotate_z(angle) * scale(half_size).
void draw_overlay_rotated_quad(overlay_batch& batch,
                               glm::vec2 center,
                               glm::vec2 half_size,
                               float angle_radians,
                               glm::vec3 color,
                               float alpha);

// Thick line as a rotated quad; zero-length segments append nothing.
void draw_overlay_segment(overlay_batch& batch,
                          glm::vec2 start,
                          glm::vec2 end,
                          float thickness,
                          glm::vec3 color,
                          float alpha);
