#include "doctest.h"

#include "renderer/overlay_batch.h"
#include "renderer/pixel_font.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec4.hpp>

#include <cmath>
#include <cstddef>
#include <string>

namespace {
// The old path drew the unit screen quad with u_mvp = model. Reproduce that
// transform on the CPU for comparison.
glm::vec2 old_matrix_corner(const glm::mat4& model, const std::size_t corner_index) {
    const std::array<float, 2>& corner = overlay_unit_quad_corners[corner_index];
    const glm::vec4 clip = model * glm::vec4(corner[0], corner[1], 0.0f, 1.0f);
    return glm::vec2(clip.x, clip.y);
}

glm::mat4 old_axis_aligned_model(const glm::vec2 center, const glm::vec2 half_size) {
    return glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(center, 0.0f)), glm::vec3(half_size, 1.0f));
}

glm::mat4 old_rotated_model(const glm::vec2 center, const glm::vec2 half_size, const float angle) {
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
        const glm::vec2 expected = old_matrix_corner(model, corner);
        CHECK(vertex.position.x == expected.x);
        CHECK(vertex.position.y == expected.y);
        CHECK(vertex.color == glm::vec4(color, alpha));
    }
}
}

TEST_CASE("overlay quad corners match the old translate*scale matrix path") {
    overlay_batch batch;
    const glm::vec2 center(0.78f, -0.31f);
    const glm::vec2 half_size(0.17f, 0.12f);
    draw_overlay_quad(batch, center, half_size, glm::vec3(0.055f, 0.06f, 0.07f), 0.82f);

    CHECK(batch.vertices.size() == overlay_vertices_per_quad);
    if (!(batch.vertices.size() == overlay_vertices_per_quad)) {
        return;
    }
    CHECK(overlay_batch_quad_count(batch) == 1U);
    check_quad_matches(batch, 0, old_axis_aligned_model(center, half_size), glm::vec3(0.055f, 0.06f, 0.07f), 0.82f);

    // Bit-exact for the axis-aligned case: no rotation terms involved.
    for (std::size_t corner = 0; corner < overlay_vertices_per_quad; ++corner) {
        CHECK(batch.vertices[corner].position == old_matrix_corner(old_axis_aligned_model(center, half_size), corner));
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

TEST_CASE("rotated overlay quad corners match the old translate*rotate*scale matrix path") {
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
        check_quad_matches(batch, 0, old_rotated_model(center, half_size, angle), glm::vec3(0.9f, 0.1f, 0.3f), 0.5f);
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
    const glm::mat4 model = old_rotated_model((start + end) * 0.5f,
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
    check_quad_matches(batch, 2, old_axis_aligned_model(glm::vec2(0.5f), glm::vec2(0.1f)), glm::vec3(0.0f, 1.0f, 0.0f), 0.9f);
}

TEST_CASE("pixel glyph appends exactly one quad per lit pixel") {
    const std::string charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789+-/:";
    for (const char c : charset) {
        overlay_batch batch;
        draw_pixel_glyph(batch, c, glm::vec2(-0.5f, 0.5f), 0.01f, glm::vec3(1.0f));
        const int lit = glyph_lit_pixel_count(c);
        CHECK(lit > 0);
        CHECK(overlay_batch_quad_count(batch) == static_cast<std::size_t>(lit));
        CHECK(batch.vertices.size() % overlay_vertices_per_quad == 0U);
    }

    overlay_batch space_batch;
    draw_pixel_glyph(space_batch, ' ', glm::vec2(0.0f), 0.01f, glm::vec3(1.0f));
    CHECK(space_batch.vertices.empty());

    overlay_batch lower;
    overlay_batch upper;
    draw_pixel_glyph(lower, 'x', glm::vec2(0.0f), 0.01f, glm::vec3(1.0f));
    draw_pixel_glyph(upper, 'X', glm::vec2(0.0f), 0.01f, glm::vec3(1.0f));
    CHECK(lower.vertices.size() == upper.vertices.size());
}

TEST_CASE("pixel glyph quads sit on the old pixel grid with the 0.42 half size") {
    overlay_batch batch;
    const glm::vec2 top_left(-0.3f, 0.2f);
    const float pixel_size = 0.015f;
    const glm::vec3 color(0.88f, 0.86f, 0.72f);
    draw_pixel_glyph(batch, 'A', top_left, pixel_size, color);

    // Walk the glyph rows in the same order the renderer does.
    const std::array<const char*, 7>& rows = glyph_rows('A');
    std::size_t quad = 0;
    for (int y = 0; y < 7; ++y) {
        for (int x = 0; rows[static_cast<std::size_t>(y)][x] != '\0'; ++x) {
            if (rows[static_cast<std::size_t>(y)][x] != '1') {
                continue;
            }
            const glm::vec2 center = top_left + glm::vec2((static_cast<float>(x) + 0.5f) * pixel_size,
                                                          -(static_cast<float>(y) + 0.5f) * pixel_size);
            CHECK(quad < overlay_batch_quad_count(batch));
            if (!(quad < overlay_batch_quad_count(batch))) {
                return;
            }
            check_quad_matches(batch, quad, old_axis_aligned_model(center, glm::vec2(pixel_size * 0.42f)), color, 1.0f);
            ++quad;
        }
    }
    CHECK(quad == overlay_batch_quad_count(batch));
}

TEST_CASE("pixel text appends the sum of its glyph quads") {
    const std::string label = "+120 XP";
    int lit = 0;
    for (const char c : label) {
        lit += glyph_lit_pixel_count(c);
    }

    overlay_batch left;
    draw_pixel_text_left(left, label, glm::vec2(0.0f), 0.01f, glm::vec3(1.0f));
    CHECK(overlay_batch_quad_count(left) == static_cast<std::size_t>(lit));

    overlay_batch centered;
    draw_pixel_text_centered(centered, label, glm::vec2(0.0f), 0.01f, glm::vec3(1.0f));
    CHECK(overlay_batch_quad_count(centered) == static_cast<std::size_t>(lit));
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
