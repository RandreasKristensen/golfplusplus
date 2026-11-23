#include "renderer/prop_renderer.h"

#include "renderer/gl_loader.h"

#include <SDL.h>

#include <cstddef>

namespace {
// A room's carts and what is held, without growing on early frames.
constexpr std::size_t initial_instance_capacity = 1024;

// The runs that take every instance of each mesh's list of `batch`.
prop_ranges whole_lists(const prop_batch& batch, const bool blended) {
    prop_ranges ranges;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const std::size_t count = batch.instances(blended, mesh).size();
        if (count > 0) {
            prop_runs& runs = ranges[static_cast<std::size_t>(mesh)];
            runs.run[0] = prop_range{0, count};
            runs.count = 1;
        }
    }
    return ranges;
}

std::array<std::size_t, prop_mesh_count> list_bases(const prop_batch& batch, const bool blended) {
    std::array<std::size_t, prop_mesh_count> bases{};
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        bases[static_cast<std::size_t>(mesh)] = batch.joined_first(blended, mesh);
    }
    return bases;
}

bool any_run(const prop_ranges& ranges) {
    for (const prop_runs& runs : ranges) {
        if (runs.count > 0) { return true; }
    }
    return false;
}
}

bool prop_renderer::init(const std::string& vertex_path, const std::string& fragment_path) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    if (!meshes_.init() || !frame_buffer_.init(initial_instance_capacity * sizeof(mesh_instance)) || !static_buffer_.init(0) ||
        !stream_buffer_.init(0)) {
        SDL_Log("The prop renderer failed to allocate GL objects.");
        shutdown();
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return true;
}

void prop_renderer::shutdown() {
    frame_buffer_.shutdown();
    static_buffer_.shutdown();
    stream_buffer_.shutdown();
    meshes_.shutdown();
    shader_.shutdown();
    static_held_ = false;
    static_opaque_ = {};
    static_ranges_ = {};
}

void prop_renderer::set_static(const prop_batch& batch, const std::uint64_t revision, frame_profile* profile) {
    if (static_buffer_.id() == 0 || (static_held_ && static_revision_ == revision)) {
        return;
    }
    batch.join(scratch_);
    for (const bool blended : {false, true}) {
        static_bases_[blended ? 1 : 0] = list_bases(batch, blended);
        static_ranges_[blended ? 1 : 0] = whole_lists(batch, blended);
    }
    for (std::size_t mesh = 0; mesh < prop_mesh_count; ++mesh) {
        static_opaque_[mesh] = prop_range{static_bases_[0][mesh], batch.instances(false, static_cast<prop_mesh>(mesh)).size()};
    }
    static_buffer_.upload(scratch_.data(), scratch_.size() * sizeof(mesh_instance), profile, upload_kind::cached);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    static_revision_ = revision;
    static_held_ = true;
}

void prop_renderer::stage(const prop_frame& frame, frame_profile* profile) {
    if (frame.batch().empty() || frame_buffer_.id() == 0) {
        return;
    }
    // One buffer: every list joined; a frame that did not change is not sent again.
    frame.batch().join(scratch_);
    frame_buffer_.upload_changed(scratch_.data(), scratch_.size() * sizeof(mesh_instance), profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void prop_renderer::begin(const glm::mat4& view_proj, const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile) {
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec4_array("u_glow_floors", glow_floors.data(), 2);
}

void prop_renderer::draw_ranges(const unsigned int buffer, const std::array<std::size_t, prop_mesh_count>& bases,
                                const prop_ranges& ranges, const bool blended, frame_profile* profile) const {
    if (!any_run(ranges)) {
        return;
    }
    if (blended) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
    }
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        const std::size_t m = static_cast<std::size_t>(mesh);
        for (int i = 0; i < ranges[m].count; ++i) {
            const prop_range& run = ranges[m].run[static_cast<std::size_t>(i)];
            meshes_.draw(buffer, mesh, bases[m] + run.first, run.count, profile);
        }
    }
    if (blended) {
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
}

void prop_renderer::draw_static(const glm::mat4& view_proj, const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile) {
    if (!static_held_ || shader_.id() == 0 || (!any_run(static_ranges_[0]) && !any_run(static_ranges_[1]))) {
        return;
    }
    begin(view_proj, glow_floors, profile);
    draw_ranges(static_buffer_.id(), static_bases_[0], static_ranges_[0], false, profile);
    draw_ranges(static_buffer_.id(), static_bases_[1], static_ranges_[1], true, profile);
}

void prop_renderer::draw_frame(const prop_frame& frame, const prop_view view, const glm::mat4& view_proj,
                               const std::array<glm::vec4, 2>& glow_floors, frame_profile* profile) {
    const prop_ranges opaque = frame.ranges(view, false);
    const prop_ranges blended = frame.ranges(view, true);
    if ((!any_run(opaque) && !any_run(blended)) || shader_.id() == 0) {
        return;
    }
    begin(view_proj, glow_floors, profile);
    draw_ranges(frame_buffer_.id(), list_bases(frame.batch(), false), opaque, false, profile);
    draw_ranges(frame_buffer_.id(), list_bases(frame.batch(), true), blended, true, profile);
}

void prop_renderer::draw(const prop_batch& batch, const glm::mat4& view_proj, frame_profile* profile) {
    if (batch.empty() || shader_.id() == 0 || stream_buffer_.id() == 0) {
        return;
    }
    begin(view_proj, std::array<glm::vec4, 2>{glm::vec4(0.0f), glm::vec4(0.0f)}, profile);
    batch.join(scratch_);
    stream_buffer_.upload_changed(scratch_.data(), scratch_.size() * sizeof(mesh_instance), profile);
    draw_ranges(stream_buffer_.id(), list_bases(batch, false), whole_lists(batch, false), false, profile);
    draw_ranges(stream_buffer_.id(), list_bases(batch, true), whole_lists(batch, true), true, profile);
}
