#include "renderer/course_map_renderer.h"

#include <SDL.h>

#include <array>
#include <cstddef>

#include "renderer/gl_loader.h"

namespace {
constexpr int sheet_texture_unit = 0;

struct sheet_vertex {
    glm::vec2 position;
    glm::vec2 uv;
};
}

bool course_map_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        shutdown();
        return false;
    }
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (vao_ == 0 || !buffer_.init(0)) {
        SDL_Log("Course map renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(sheet_vertex), reinterpret_cast<void*>(offsetof(sheet_vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(sheet_vertex), reinterpret_cast<void*>(offsetof(sheet_vertex, uv)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    shader_.use();
    shader_.set_int("u_sheet", sheet_texture_unit);
    return true;
}

void course_map_renderer::shutdown() {
    texture_.shutdown();
    buffer_.shutdown();
    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.shutdown();
}

void course_map_renderer::draw(const course_map_sheet& sheet, const map_inks& inks, const std::uint64_t inks_revision,
                               const glm::vec2 center, const glm::vec2 half_size, frame_profile* profile) {
    if (vao_ == 0 || shader_.id() == 0) {
        return;
    }
    if (!texture_.loaded() || painted_sheet_ != sheet.revision || painted_inks_ != inks_revision) {
        texture_.upload(paint_course_map_sheet(sheet, inks), texture_sampling::texels);
        painted_sheet_ = sheet.revision;
        painted_inks_ = inks_revision;
    }
    if (!texture_.loaded()) {
        return;
    }
    // Row 0 of the image is its bottom.
    const glm::vec2 low = center - half_size;
    const glm::vec2 high = center + half_size;
    const std::array<sheet_vertex, 6> quad{{{low, {0.0f, 0.0f}},
                                            {{high.x, low.y}, {1.0f, 0.0f}},
                                            {high, {1.0f, 1.0f}},
                                            {low, {0.0f, 0.0f}},
                                            {high, {1.0f, 1.0f}},
                                            {{low.x, high.y}, {0.0f, 1.0f}}}};
    shader_.set_profile(profile);
    shader_.use();
    texture_.bind(sheet_texture_unit);
    glBindVertexArray(vao_);
    buffer_.upload(quad.data(), sizeof(quad), profile);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(quad.size()));
    record_draw_call(profile);
    glBindVertexArray(0);
}
