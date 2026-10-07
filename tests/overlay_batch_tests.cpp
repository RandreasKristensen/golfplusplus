#include "doctest.h"

#include "renderer/overlay_batch.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>

namespace {
// A quad is the unit screen quad moved by its model matrix (translate,
// rotate, scale). The batch writes the corners directly; this gives them
// through the matrix, for comparison.
glm::vec2 matrix_corner(const glm::mat4& model, const std::size_t corner_index) {
    const std::array<float, 2>& corner = overlay_unit_quad_corners[corner_index];
    const glm::vec4 clip = model * glm::vec4(corner[0], corner[1], 0.0f, 1.0f);
    return glm::vec2(clip.x, clip.y);
}

glm::mat4 axis_aligned_model(const glm::vec2 center, const glm::vec2 half_size) {
    return glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)), glm::vec3(half_size, 1.0f));
}

glm::mat4 rotated_model(const glm::vec2 center, const glm::vec2 half_size, const float angle) {
    glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f));
    model = glm::rotate(model, angle, glm::vec3(0.0f, 0.0f, 1.0f));
    return glm::scale(model, glm::vec3(half_size, 1.0f));
}

void check_quad_matches(const overlay_batch& batch,
                        const std::size_t quad_index,
                        const glm::mat4& model,
                        const glm::vec3 color,
                        const float alpha) {
    for (std::size_t corner = 0; corner < overlay_vertices_per_quad; ++corner) {
        const overlay_vertex& vertex = batch.vertices[quad_index * overlay_vertices_per_quad + corner];
        const glm::vec2 expected = matrix_corner(model, corner);
        CHECK(vertex.position.x == expected.x);
        CHECK(vertex.position.y == expected.y);
        CHECK(vertex.color == glm::vec4(color, alpha));
    }
}
}

TEST_CASE("overlay quad corners match the translate*scale model matrix") {
    overlay_batch batch;
    const glm::vec2 center(0.78f, -0.31f);
    const glm::vec2 half_size(0.17f, 0.12f);
    draw_overlay_quad(batch, center, half_size, glm::vec3(0.055f, 0.06f, 0.07f), 0.82f);

    CHECK(batch.vertices.size() == overlay_vertices_per_quad);
    if (!(batch.vertices.size() == overlay_vertices_per_quad)) {
        return;
    }
    CHECK(overlay_batch_quad_count(batch) == 1U);
    check_quad_matches(batch, 0, axis_aligned_model(center, half_size), glm::vec3(0.055f, 0.06f, 0.07f), 0.82f);

    // Bit-exact for the axis-aligned case: no rotation terms involved.
    for (std::size_t corner = 0; corner < overlay_vertices_per_quad; ++corner) {
        CHECK(batch.vertices[corner].position == matrix_corner(axis_aligned_model(center, half_size), corner));
    }
}

TEST_CASE("overlay quad defaults to opaque alpha") {
    overlay_batch batch;
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.2f, 0.4f, 0.6f));
    CHECK(batch.vertices.size() == overlay_vertices_per_quad);
    if (!(batch.vertices.size() == overlay_vertices_per_quad)) {
        return;
    }
    CHECK(batch.vertices[0].color.a == 1.0f);
}

TEST_CASE("rotated overlay quad corners match the translate*rotate*scale model matrix") {
    const float angles[] = {0.0f, 0.37f, 1.5707964f, -2.25f, 3.14159265f, 5.9f};
    for (const float angle : angles) {
        overlay_batch batch;
        const glm::vec2 center(-0.42f, 0.66f);
        const glm::vec2 half_size(0.21f, 0.004f);
        draw_overlay_rotated_quad(batch, center, half_size, angle, glm::vec3(0.9f, 0.1f, 0.3f), 0.5f);
        CHECK(batch.vertices.size() == overlay_vertices_per_quad);
        if (!(batch.vertices.size() == overlay_vertices_per_quad)) {
            return;
        }
        check_quad_matches(batch, 0, rotated_model(center, half_size, angle), glm::vec3(0.9f, 0.1f, 0.3f), 0.5f);
    }
}

TEST_CASE("overlay segment is a rotated quad along the segment and skips zero length") {
    overlay_batch batch;
    const glm::vec2 start(-0.2f, 0.1f);
    const glm::vec2 end(0.3f, -0.4f);
    draw_overlay_segment(batch, start, end, 0.008f, glm::vec3(0.5f), 0.75f);
    CHECK(overlay_batch_quad_count(batch) == 1U);
    if (!(overlay_batch_quad_count(batch) == 1U)) {
        return;
    }

    const glm::vec2 delta = end - start;
    const glm::mat4 model = rotated_model((start + end) * 0.5f,
                                              glm::vec2(std::sqrt(delta.x * delta.x + delta.y * delta.y) * 0.5f, 0.004f),
                                              std::atan2(delta.y, delta.x));
    check_quad_matches(batch, 0, model, glm::vec3(0.5f), 0.75f);

    draw_overlay_segment(batch, start, start, 0.008f, glm::vec3(0.5f), 0.75f);
    CHECK(overlay_batch_quad_count(batch) == 1U);
}

TEST_CASE("overlay batch keeps submission order for painter's-order blending") {
    overlay_batch batch;
    draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(1.0f), glm::vec3(0.0f), 0.72f);
    draw_overlay_rotated_quad(batch, glm::vec2(0.1f), glm::vec2(0.2f), 0.5f, glm::vec3(1.0f, 0.0f, 0.0f), 0.3f);
    draw_overlay_quad(batch, glm::vec2(0.5f), glm::vec2(0.1f), glm::vec3(0.0f, 1.0f, 0.0f), 0.9f);

    CHECK(overlay_batch_quad_count(batch) == 3U);
    if (!(overlay_batch_quad_count(batch) == 3U)) {
        return;
    }
    for (std::size_t corner = 0; corner < overlay_vertices_per_quad; ++corner) {
        CHECK(batch.vertices[corner].color == glm::vec4(0.0f, 0.0f, 0.0f, 0.72f));
        CHECK(batch.vertices[overlay_vertices_per_quad + corner].color == glm::vec4(1.0f, 0.0f, 0.0f, 0.3f));
        CHECK(batch.vertices[2 * overlay_vertices_per_quad + corner].color == glm::vec4(0.0f, 1.0f, 0.0f, 0.9f));
    }
    check_quad_matches(batch, 2, axis_aligned_model(glm::vec2(0.5f), glm::vec2(0.1f)), glm::vec3(0.0f, 1.0f, 0.0f), 0.9f);
}

TEST_CASE("clearing the overlay batch keeps its capacity for reuse") {
    overlay_batch batch;
    for (int i = 0; i < 500; ++i) {
        draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(0.1f), glm::vec3(1.0f), 1.0f);
    }
    const std::size_t capacity = batch.vertices.capacity();
    CHECK(capacity >= 500U * overlay_vertices_per_quad);
    if (!(capacity >= 500U * overlay_vertices_per_quad)) {
        return;
    }

    clear_overlay_batch(batch);
    CHECK(batch.vertices.empty());
    CHECK(overlay_batch_quad_count(batch) == 0U);
    CHECK(batch.vertices.capacity() == capacity);

    const overlay_vertex* storage = batch.vertices.data();
    for (int i = 0; i < 500; ++i) {
        draw_overlay_quad(batch, glm::vec2(0.0f), glm::vec2(0.1f), glm::vec3(1.0f), 1.0f);
    }
    CHECK(batch.vertices.capacity() == capacity);
    CHECK(batch.vertices.data() == storage);
}
