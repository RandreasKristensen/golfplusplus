#include "renderer/tree_renderer.h"
#include "renderer/tree_model.h"

#include <SDL.h>

#include <cstddef>
#include <type_traits>

#include <glm/common.hpp>

#include "renderer/gl_loader.h"

namespace {
constexpr GLuint position_location = 0;
constexpr GLuint normal_location = 1;
constexpr GLuint leaf_location = 2;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
constexpr GLuint instance_turn_location = 5;

// The colour a species' leaves are on the course's date: fresh, then
// summer green, then turned.
glm::vec3 leaf_colour(const tree_look& look, const render_tree_species& season) {
    const glm::vec3 green = glm::mix(look.summer, look.spring, glm::clamp(season.fresh, 0.0f, 1.0f));
    return glm::mix(green, look.autumn, glm::clamp(season.turned, 0.0f, 1.0f));
}
}

static_assert(sizeof(render_tree_instance) == 8 * sizeof(float), "render_tree_instance must be eight packed floats");
static_assert(offsetof(render_tree_instance, scale) == 3 * sizeof(float), "render_tree_instance::scale must follow offset");
static_assert(std::is_standard_layout<render_tree_instance>::value, "render_tree_instance must be standard layout");
static_assert(std::is_standard_layout<grown_tree_vertex>::value, "grown_tree_vertex must be standard layout");

bool tree_renderer::init(const std::string& vertex_path, const std::string& fragment_path, const std::vector<tree_look>& looks) {
    shutdown();
    if (!shader_.load_from_files(vertex_path, fragment_path) || !instances_.init(0)) {
        shutdown();
        return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    return set_looks(looks);
}

void tree_renderer::shutdown() {
    release_meshes();
    instances_.shutdown();
    shader_.shutdown();
    seasons_.clear();
}

void tree_renderer::release_meshes() {
    for (model_mesh& mesh : meshes_) {
        if (mesh.vbo != 0) {
            glDeleteBuffers(1, &mesh.vbo);
        }
        if (mesh.vao != 0) {
            glDeleteVertexArrays(1, &mesh.vao);
        }
    }
    meshes_.clear();
}

bool tree_renderer::set_looks(const std::vector<tree_look>& looks) {
    release_meshes();
    looks_ = looks;
    constexpr GLsizei stride = sizeof(grown_tree_vertex);
    for (std::size_t look = 0; look < looks_.size(); ++look) {
        for (int variant = 0; variant < tree_variants; ++variant) {
            const grown_tree model = grow_tree(looks_[look], static_cast<std::uint32_t>(look * 31 + variant * 7 + 1));
            model_mesh mesh;
            glGenVertexArrays(1, &mesh.vao);
            glGenBuffers(1, &mesh.vbo);
            if (mesh.vao == 0 || mesh.vbo == 0) {
                SDL_Log("Tree renderer failed to allocate GL objects.");
                meshes_.push_back(mesh);
                release_meshes();
                return false;
            }
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(model.vertices.size() * sizeof(grown_tree_vertex)),
                         model.vertices.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(position_location);
            glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, position)));
            glEnableVertexAttribArray(normal_location);
            glVertexAttribPointer(normal_location, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, normal)));
            glEnableVertexAttribArray(leaf_location);
            glVertexAttribPointer(leaf_location, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, uv)));
            for (const GLuint location : {instance_offset_location, instance_scale_location, instance_turn_location}) {
                glEnableVertexAttribArray(location);
                glVertexAttribDivisor(location, 1);
            }
            glBindVertexArray(0);
            mesh.bark_count = static_cast<int>(model.bark_count);
            mesh.vertex_count = static_cast<int>(model.vertices.size());
            meshes_.push_back(mesh);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ++model_revision_;
    return true;
}

void tree_renderer::set_seasons(const std::vector<render_tree_species>& species) {
    seasons_.clear();
    for (const render_tree_species& season : species) {
        species_season shown;
        for (std::size_t look = 0; look < looks_.size(); ++look) {
            if (looks_[look].name == season.name) {
                shown.look = look;
            }
        }
        shown.foliage = season.foliage;
        shown.leaves = looks_.empty() ? glm::vec3(0.0f) : leaf_colour(looks_[shown.look], season);
        seasons_.push_back(shown);
    }
}

const tree_renderer::model_mesh* tree_renderer::mesh_for(const std::size_t look, const int variant) const {
    const std::size_t index = look * tree_variants + static_cast<std::size_t>(variant);
    return index < meshes_.size() ? &meshes_[index] : nullptr;
}

template <typename Draw>
void tree_renderer::draw_batch(const render_tree_batch& batch, frame_profile* profile, Draw draw_bucket) {
    if (batch.instances.empty()) {
        return;
    }
    instances_.upload(batch.instances.data(), batch.instances.size() * sizeof(render_tree_instance), profile);
    constexpr GLsizei stride = sizeof(render_tree_instance);
    for (const render_tree_bucket& bucket : batch.buckets) {
        if (bucket.species >= seasons_.size()) {
            continue;
        }
        const model_mesh* mesh = mesh_for(seasons_[bucket.species].look, bucket.variant);
        if (mesh == nullptr) {
            continue;
        }
        glBindVertexArray(mesh->vao);
        glBindBuffer(GL_ARRAY_BUFFER, instances_.id());
        const std::size_t first = bucket.first * sizeof(render_tree_instance);
        glVertexAttribPointer(instance_offset_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, offset)));
        glVertexAttribPointer(instance_scale_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, scale)));
        glVertexAttribPointer(instance_turn_location, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, yaw)));
        draw_bucket(bucket, *mesh, seasons_[bucket.species]);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool tree_renderer::draw(const std::vector<tree_body>& trees, const glm::mat4& view_proj, const view_frustum& frustum,
                         const float clock_seconds, const glm::vec2& wind, frame_profile* profile) {
    if (shader_.id() == 0 || trees.empty()) {
        return false;
    }
    set_motion(clock_seconds, wind);
    const render_tree_batch batch = build_tree_batch(trees, &frustum);
    if (batch.instances.empty()) {
        return false;
    }
    shader_.set_profile(profile);
    shader_.use();
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_float("u_time", clock_seconds);
    shader_.set_vec2("u_wind", wind);
    draw_batch(batch, profile, [&](const render_tree_bucket& bucket, const model_mesh& mesh, const species_season& season) {
        const tree_look& look = looks_[season.look];
        shader_.set_vec3("u_bark_color", look.bark);
        shader_.set_vec3("u_leaf_color", season.leaves);
        shader_.set_float("u_foliage", season.foliage);
        shader_.set_float("u_shine_through", look.shine_through);
        const int count = season.foliage > 0.0f ? mesh.vertex_count : mesh.bark_count;
        glDrawArraysInstanced(GL_TRIANGLES, 0, count, static_cast<GLsizei>(bucket.count));
        record_draw_call(profile);
    });
    return true;
}

void tree_renderer::draw_casters(const std::vector<tree_body>& trees, std::uint64_t, const shader_program& depth_shader,
                                 const glm::mat4& view_proj, frame_profile* profile) {
    if (trees.empty()) {
        return;
    }
    const view_frustum frustum = make_view_frustum(view_proj);
    const render_tree_batch batch = build_tree_batch(trees, &frustum);
    depth_shader.set_mat4("u_view_proj", view_proj);
    depth_shader.set_float("u_time", seconds_);
    depth_shader.set_vec2("u_wind", wind_);
    draw_batch(batch, profile, [&](const render_tree_bucket& bucket, const model_mesh& mesh, const species_season& season) {
        depth_shader.set_float("u_foliage", season.foliage);
        const int count = season.foliage > 0.0f ? mesh.vertex_count : mesh.bark_count;
        glDrawArraysInstanced(GL_TRIANGLES, 0, count, static_cast<GLsizei>(bucket.count));
        record_draw_call(profile);
    });
}
