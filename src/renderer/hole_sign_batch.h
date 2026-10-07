#pragma once

// The hole signs as the scene draws them: one face texture per sign
// (hole_sign_face.h) and every sign's model in one vertex list, its board
// with the texture on the face and wood elsewhere, on two posts. Painting
// the faces samples the ground for every texel, so it is rebuilt only when
// the signs themselves change (a new course), never per frame or per hole.
// GL-free; renderer/hole_sign_renderer draws it.

#include "game/hole_sign.h"
#include "game/play_area.h"
#include "game/text_assets.h"
#include "renderer/bmp_image.h"

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

struct hole_sign_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{0.0f};  // used where `textured` is 0
    glm::vec2 uv{0.0f};
    float textured = 0.0f;  // 1 on the face: the texture instead of `color`
};

struct render_hole_sign {
    rgba_image face;
    std::size_t first_vertex = 0;  // its model's range in render_hole_signs::vertices
    std::size_t vertex_count = 0;
};

// What one sign's face says (core/render_frame.h hole_sign_texts).
struct hole_sign_text {
    int number = 0;  // the course hole, from 1
    int par = 0;
    int meters = 0;
};

// What a sign was built from: what it says, and where it stands.
struct hole_sign_source {
    hole_sign_text text;
    glm::vec3 board_center{0.0f};
};

struct render_hole_signs {
    std::vector<hole_sign_vertex> vertices;  // triangles
    std::vector<render_hole_sign> signs;
    std::vector<hole_sign_source> sources;  // one per sign of the play area
    // Bumped when rebuilt; the GL side re-uploads only when it changes.
    std::uint64_t revision = 0;
    // Labels on the faces that did not fit their boxes (tests want none).
    std::size_t truncated_text_count = 0;
};

// Appends `sign`'s board and posts as triangles.
void append_hole_sign_model(std::vector<hole_sign_vertex>& vertices, const hole_sign& sign);

// Every sign of `area`, each saying `texts[i]` (one per area.signs[i]).
render_hole_signs build_render_hole_signs(const play_area& area,
                                          const std::vector<hole_sign_text>& texts,
                                          const text_assets& text,
                                          std::uint64_t revision);
// Rebuilds `signs` (with `revision`) unless they were built from the same
// signs saying the same; true when it did.
bool refresh_render_hole_signs(render_hole_signs& signs,
                               const play_area& area,
                               const std::vector<hole_sign_text>& texts,
                               const text_assets& text,
                               std::uint64_t revision);
