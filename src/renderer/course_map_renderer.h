#pragma once

// GL side of the paper course map's ground: the sheet's painted image
// (course_map_sheet.h) as one texture drawn over the paper as a single quad
// in overlay clip space. The image is painted and uploaded again only when
// the sheet or the inks change.

#include "profiling/profiling.h"
#include "game/lighting_loader.h"
#include "renderer/course_map_sheet.h"
#include "renderer/dynamic_buffer.h"
#include "renderer/gl_storage.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

#include <cstdint>
#include <string>

#include <glm/vec2.hpp>

class course_map_renderer {
public:
    explicit course_map_renderer(gl_storage storage = {}) : shader_(storage), buffer_(storage), texture_(storage) {}

    bool init(const std::string& vertex_path, const std::string& fragment_path);
    void shutdown();

    // Draws `sheet` in `inks` (paint_course_map_sheet) over the rectangle `center` +/- `half_size` (overlay clip space, blended).
    // `inks_revision` changes whenever `inks` does. Flush the overlay batch
    // first so painter's order holds.
    void draw(const course_map_sheet& sheet, const map_inks& inks, std::uint64_t inks_revision, glm::vec2 center, glm::vec2 half_size,
              frame_profile* profile);

private:
    // What the texture was painted from.
    std::uint64_t painted_sheet_ = 0;
    std::uint64_t painted_inks_ = 0;

    shader_program shader_;
    unsigned int vao_ = 0;
    dynamic_vertex_buffer buffer_;
    texture texture_;
};
