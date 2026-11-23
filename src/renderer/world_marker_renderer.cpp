#include "renderer/world_marker_renderer.h"

#include <SDL.h>

#include <array>
#include <cstddef>
#include <type_traits>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint color_location = 1;
constexpr GLuint glow_location = 2;

// The GL state a run's stencil use needs (world_marker_batch.h); none puts
// back what every other pass expects.
void use_stencil(const marker_stencil stencil) {
    switch (stencil) {
    case marker_stencil::none:
        glDisable(GL_STENCIL_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_LESS);
        break;
    case marker_stencil::mark_holes:
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_ALWAYS, 0, 0xFF);
        glStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_INCR_WRAP);
        glStencilOpSeparate(GL_BACK, GL_KEEP, GL_KEEP, GL_DECR_WRAP);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthFunc(GL_LESS);
        break;
    case marker_stencil::hole_insides:
        // A cup is convex: of its faces turned towards the camera only one
        // is on any pixel, so they need no depth test among themselves.
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(GL_NOTEQUAL, 0, 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthFunc(GL_ALWAYS);
        break;
    }
}

// Covers an 18-hole course's tee boxes (~280 vertices each), its cups and
// flagsticks, aim dots and club without growing on early frames; a room's
// carts and players grow it.
constexpr std::size_t initial_vertex_capacity = 12288;
// A room's figures and balls, without growing on early frames.
constexpr std::size_t initial_instance_capacity = 1024;
}

// The vertex buffer is uploaded as raw floats: position.xyz, color.rgb, glow.
static_assert(sizeof(world_marker_vertex) == 7 * sizeof(float), "world_marker_vertex must be seven packed floats");
static_assert(offsetof(world_marker_vertex, color) == 3 * sizeof(float), "world_marker_vertex::color must follow position");
static_assert(offsetof(world_marker_vertex, glow) == 6 * sizeof(float), "world_marker_vertex::glow must follow color");
static_assert(std::is_standard_layout<world_marker_vertex>::value, "world_marker_vertex must be standard layout");

bool world_marker_renderer::init(const std::string& vertex_path, const std::string& fragment_path, const bool instanced) {
    shutdown();

    if (!shader_.load_from_files(vertex_path, fragment_path) ||
        (instanced && (!instanced_shader_.load_from_files(vertex_path, fragment_path, "#define INSTANCED\n") || !meshes_.init() ||
                       !instance_buffer_.init(initial_instance_capacity * sizeof(mesh_instance))))) {
        shutdown();
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glGenVertexArrays(1, &vao_);
    if (vao_ == 0) {
        SDL_Log("World marker renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }

    glBindVertexArray(vao_);
    // init() leaves the buffer bound for the attribute pointers below.
    if (!buffer_.init(initial_vertex_capacity * sizeof(world_marker_vertex))) {
        SDL_Log("World marker renderer failed to allocate GL objects.");
        glBindVertexArray(0);
        shutdown();
        return false;
    }
    glEnableVertexAttribArray(position_location);
    glVertexAttribPointer(position_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, position)));
    glEnableVertexAttribArray(color_location);
    glVertexAttribPointer(color_location,
                          3,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, color)));
    glEnableVertexAttribArray(glow_location);
    glVertexAttribPointer(glow_location,
                          1,
                          GL_FLOAT,
                          GL_FALSE,
                          sizeof(world_marker_vertex),
                          reinterpret_cast<void*>(offsetof(world_marker_vertex, glow)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void world_marker_renderer::shutdown() {
    buffer_.shutdown();
    instance_buffer_.shutdown();
    meshes_.shutdown();
    instanced_shader_.shutdown();

    if (vao_ != 0) {
        glDeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }

    shader_.shutdown();
}

void world_marker_renderer::stage(const world_marker_batch& batch, frame_profile* profile, const upload_kind kind) {
    if (batch.empty() || vao_ == 0) {
        return;
    }
    const std::size_t bytes = batch.vertices().size() * sizeof(world_marker_vertex);
    if (kind == upload_kind::dynamic) {
        std::array<std::size_t, static_cast<std::size_t>(marker_part::count)> ends{};
        for (std::size_t p = 0; p < batch.part_end_count(); ++p) { ends[p] = batch.part_end(p) * sizeof(world_marker_vertex); }
        buffer_.upload_changed(batch.vertices().data(), bytes, ends.data(), batch.part_end_count(), profile);
    } else {
        buffer_.upload(batch.vertices().data(), bytes, profile, kind);
    }
    if (instance_buffer_.id() != 0 && batch.instance_count() > 0) {
        batch.join_instances(scratch_);
        instance_buffer_.upload_changed(scratch_.data(), scratch_.size() * sizeof(mesh_instance), profile);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void world_marker_renderer::draw(const world_marker_batch& batch, const marker_view view, const glm::mat4& view_proj,
                                 const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile) {
    if (batch.empty() || shader_.id() == 0 || vao_ == 0) {
        return;
    }

    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec4_array("u_glow_floors", glow_floors.data(), 2);

    glBindVertexArray(vao_);
    for (const world_marker_run& run : batch.runs()) {
        if (run.count == 0 || !batch.shown_in(run, view)) {
            continue;
        }
        use_stencil(run.stencil);
        glDepthMask(run.depth_write ? GL_TRUE : GL_FALSE);
        if (run.layer > 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-static_cast<float>(run.layer), -static_cast<float>(run.layer));
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        glDrawArrays(GL_TRIANGLES, static_cast<GLint>(run.first), static_cast<GLsizei>(run.count));
        record_draw_call(profile);
    }
    use_stencil(marker_stencil::none);
    glDepthMask(GL_TRUE);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindVertexArray(0);

    if (instance_buffer_.id() == 0 || batch.instance_runs().empty()) {
        return;
    }
    instanced_shader_.set_profile(profile);
    instanced_shader_.use();
    instanced_shader_.set_mat4("u_view_proj", view_proj);
    instanced_shader_.set_vec4_array("u_glow_floors", glow_floors.data(), 2);
    for (const world_marker_instance_run& run : batch.instance_runs()) {
        if (batch.shown_in(run.part, view)) {
            meshes_.draw(instance_buffer_.id(), run.mesh, batch.instance_base(run.mesh) + run.first, run.count, profile);
        }
    }
}
