#include "renderer/tree_renderer.h"
#include "renderer/tree_model.h"

#include <SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
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
constexpr GLuint card_location = 6;

// tree_leaf.glsl's twigs: zig-zag, hang, the cards' length over width, haze.
glm::vec4 twigs_of(const tree_look& look) {
    return glm::vec4(look.twig_kink, look.twig_hang, 1.0f + look.droop, look.twig_haze);
}

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

bool tree_renderer::init(const std::string& vertex_path, const std::string& fragment_path, const std::vector<tree_look>& looks,
                         const tree_impostor_style& style) {
    shutdown();
    shader_directory_ = std::filesystem::path(vertex_path).parent_path().string();
    const auto beside = [&](const char* name) { return (std::filesystem::path(shader_directory_) / name).string(); };
    if (!shader_.load_from_files(vertex_path, fragment_path) ||
        !depth_shader_.load_from_files(beside("shadow_map.vert"), beside("shadow_map.frag"), "#define TREE_CASTER\n") ||
        !leaves_.init(beside("falling_leaf.vert"), beside("falling_leaf.frag"))) {
        shutdown();
        return false;
    }
    glGenBuffers(1, &instance_vbo_);
    return set_looks(looks, style);
}

void tree_renderer::shutdown() {
    release_meshes();
    images_.shutdown();
    baked_seasons_.clear();
    storage_.delete_buffer(instance_vbo_);
    index_ = tree_index{};
    trees_revision_ = 0;
    shader_.shutdown();
    depth_shader_.shutdown();
    leaves_.shutdown();
    seasons_.clear();
}

void tree_renderer::release_meshes() {
    for (model_mesh& mesh : meshes_) {
        storage_.delete_buffer(mesh.vbo);
        storage_.delete_buffer(mesh.ebo);
        if (mesh.vao != 0) {
            glDeleteVertexArrays(1, &mesh.vao);
        }
    }
    meshes_.clear();
}

bool tree_renderer::set_looks(const std::vector<tree_look>& looks, const tree_impostor_style& style) {
    release_meshes();
    looks_ = looks;
    baked_seasons_.clear();
    const auto in_directory = [&](const char* name) { return (std::filesystem::path(shader_directory_) / name).string(); };
    if (!images_.init(in_directory("tree_bake.vert"), in_directory("tree_bake.frag"), in_directory("tree_image.vert"),
                      in_directory("tree_image.frag"), style, looks_.size() * static_cast<std::size_t>(tree_variants))) {
        return false;
    }
    constexpr GLsizei stride = sizeof(grown_tree_vertex);
    for (std::size_t look = 0; look < looks_.size(); ++look) {
        for (int variant = 0; variant < tree_variants; ++variant) {
            const grown_tree model = grow_tree(looks_[look], static_cast<std::uint32_t>(look * 31 + variant * 7 + 1));
            model_mesh mesh;
            glGenVertexArrays(1, &mesh.vao);
            glGenBuffers(1, &mesh.vbo);
            glGenBuffers(1, &mesh.ebo);
            if (mesh.vao == 0 || mesh.vbo == 0 || mesh.ebo == 0) {
                SDL_Log("Tree renderer failed to allocate GL objects.");
                meshes_.push_back(mesh);
                release_meshes();
                return false;
            }
            glBindVertexArray(mesh.vao);
            glBindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
            storage_.buffer(mesh.vbo, GL_ARRAY_BUFFER, model.vertices.size() * sizeof(grown_tree_vertex),
                         model.vertices.data(), GL_STATIC_DRAW);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
            storage_.buffer(mesh.ebo, GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(std::uint32_t),
                            model.indices.data(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(position_location);
            glVertexAttribPointer(position_location, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, position)));
            glEnableVertexAttribArray(normal_location);
            glVertexAttribPointer(normal_location, 3, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, normal)));
            glEnableVertexAttribArray(leaf_location);
            glVertexAttribPointer(leaf_location, 4, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, uv)));
            glEnableVertexAttribArray(card_location);
            glVertexAttribPointer(card_location, 2, GL_FLOAT, GL_FALSE, stride,
                                  reinterpret_cast<const void*>(offsetof(grown_tree_vertex, hold)));
            for (const GLuint location : {instance_offset_location, instance_scale_location, instance_turn_location}) {
                glEnableVertexAttribArray(location);
                glVertexAttribDivisor(location, 1);
            }
            glBindVertexArray(0);
            mesh.box = tree_box_of(model);
            mesh.index_count = static_cast<int>(model.indices.size());
            meshes_.push_back(mesh);
        }
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    ++model_revision_;
    return true;
}

void tree_renderer::set_seasons(const std::vector<render_tree_species>& species, frame_profile* profile) {
    seasons_.clear();
    for (const render_tree_species& season : species) {
        species_season shown;
        for (std::size_t look = 0; look < looks_.size(); ++look) {
            if (looks_[look].name == season.name) {
                shown.look = look;
            }
        }
        shown.foliage = season.foliage;
        shown.withered = season.withered;
        shown.falling = season.falling;
        shown.falling_withered = season.falling_withered;
        shown.leaves = looks_.empty() ? glm::vec3(0.0f) : leaf_colour(looks_[shown.look], season);
        seasons_.push_back(shown);
    }
    bake_images(profile);
}

void tree_renderer::bake_images(frame_profile* profile) {
    if (seasons_ == baked_seasons_ && baked_models_ == model_revision_) {
        return;
    }
    images_.forget_all();
    images_.begin_bake(profile);
    for (std::size_t i = 0; i < seasons_.size(); ++i) {
        const species_season& season = seasons_[i];
        const bool again = std::any_of(seasons_.begin(), seasons_.begin() + static_cast<std::ptrdiff_t>(i),
                                       [&](const species_season& other) { return other.look == season.look; });
        if (again || season.look >= looks_.size()) {
            continue;
        }
        for (int variant = 0; variant < tree_variants; ++variant) {
            const model_mesh* mesh = mesh_for(season.look, variant);
            if (mesh == nullptr) {
                continue;
            }
            const tree_look& look = looks_[season.look];
            images_.bake(season.look * static_cast<std::size_t>(tree_variants) + static_cast<std::size_t>(variant),
                         tree_image_source{mesh->vao, mesh->index_count, mesh->box, look.bark, look.twig, season.leaves,
                                           look.withered, glm::vec2(season.foliage, season.withered),
                                           twigs_of(look), look.shine_through});
        }
    }
    images_.end_bake();
    baked_seasons_ = seasons_;
    baked_models_ = model_revision_;
}

void tree_renderer::set_card_uniforms(const shader_program& program, const species_season& season) const {
    const tree_look& look = looks_[season.look];
    program.set_vec2("u_season", glm::vec2(season.foliage, season.withered));
    program.set_vec4("u_twigs", twigs_of(look));
}

const tree_renderer::model_mesh* tree_renderer::mesh_for(const std::size_t look, const int variant) const {
    const std::size_t index = look * tree_variants + static_cast<std::size_t>(variant);
    return index < meshes_.size() ? &meshes_[index] : nullptr;
}

void tree_renderer::set_trees(const std::vector<tree_body>& trees, const std::uint64_t revision, frame_profile* profile) {
    if (instance_vbo_ == 0 || (trees_revision_ == revision && tree_count() == trees.size())) {
        return;
    }
    index_.build(trees);
    trees_revision_ = revision;
    if (index_.empty()) {
        return;
    }
    largest_tree_ = render_tree_instance{};
    for (const render_tree_instance& instance : index_.instances()) {
        largest_tree_.scale = glm::max(largest_tree_.scale, instance.scale);
    }
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    storage_.buffer(instance_vbo_, GL_ARRAY_BUFFER, index_.instances().size() * sizeof(render_tree_instance),
                    index_.instances().data(), GL_STATIC_DRAW, profile);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

template <typename Draw>
void tree_renderer::draw_selected(frame_profile* profile, const bool counted, Draw set_bucket_uniforms) {
    if (selected_.empty()) {
        return;
    }
    constexpr GLsizei stride = sizeof(render_tree_instance);
    glBindBuffer(GL_ARRAY_BUFFER, instance_vbo_);
    for (const render_tree_bucket& bucket : selected_) {
        if (bucket.species >= seasons_.size()) {
            continue;
        }
        const model_mesh* mesh = mesh_for(seasons_[bucket.species].look, bucket.variant);
        if (mesh == nullptr) {
            continue;
        }
        glBindVertexArray(mesh->vao);
        const std::size_t first = bucket.first * sizeof(render_tree_instance);
        glVertexAttribPointer(instance_offset_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, offset)));
        glVertexAttribPointer(instance_scale_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, scale)));
        glVertexAttribPointer(instance_turn_location, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, yaw)));
        const auto& season = seasons_[bucket.species];
        const int count = mesh->index_count;
        set_bucket_uniforms(season);
        glDrawElementsInstanced(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr, static_cast<GLsizei>(bucket.count));
        if (profile && counted) {
            auto& work = profile->work.current();
            work.tree_instances += bucket.count;
            work.tree_triangles += static_cast<std::uint64_t>(count / 3) * bucket.count;
        }
        record_draw_call(profile);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

bool tree_renderer::draw(const glm::mat4& view_proj, const view_frustum& frustum, const float clock_seconds,
                         const glm::vec2& wind, frame_profile* profile, const tree_eye& eye) {
    if (shader_.id() == 0 || index_.empty()) {
        return false;
    }
    set_motion(clock_seconds, wind);
    const tree_impostor_style& style = images_.style();
    tree_lod_view lod;
    const bool images_ready = eye.focal_pixels > 0.0f && seasons_ == baked_seasons_ && baked_models_ == model_revision_;
    if (images_ready) {
        lod = {eye.position, eye.focal_pixels, style.max_error_pixels, style.radius_error, style.wind_error, glm::length(wind)};
    }
    double near_pixels = 0.0;
    index_.select(&frustum, &lod, selected_, far_selected_, &near_pixels);
    if (selected_.empty() && far_selected_.empty()) {
        return false;
    }
    if (!selected_.empty()) {
        bool depth_first = false;
        // Where crowns cover the picture many times over, leaves overlap deep: depth first, so each pixel is lit once.
        depth_first = eye.viewport_pixels > 0.0f && near_pixels > (eye.lamps ? style.depth_first_coverage_lamps : style.depth_first_coverage) * eye.viewport_pixels;
        if (depth_first) {
            depth_shader_.set_profile(profile);
            depth_shader_.use();
            depth_shader_.set_mat4("u_view_proj", view_proj);
            depth_shader_.set_float("u_time", clock_seconds);
            depth_shader_.set_vec2("u_wind", wind);
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            draw_selected(profile, false, [&](const species_season& season) { set_card_uniforms(depth_shader_, season); });
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
        }
        shader_.set_profile(profile);
        shader_.use();
        shader_.set_mat4("u_view_proj", view_proj);
        shader_.set_float("u_time", clock_seconds);
        shader_.set_vec2("u_wind", wind);
        draw_selected(profile, true, [&](const species_season& season) {
            const tree_look& look = looks_[season.look];
            shader_.set_vec3("u_bark_color", look.bark);
            shader_.set_vec3("u_twig_color", look.twig);
            shader_.set_vec3("u_leaf_color", season.leaves);
            shader_.set_vec3("u_withered_color", look.withered);
            shader_.set_float("u_shine_through", look.shine_through);
            set_card_uniforms(shader_, season);
        });
        if (depth_first) {
            glDepthMask(GL_TRUE);
            glDepthFunc(GL_LESS);
        }
        draw_falling_leaves(view_proj, eye, clock_seconds, wind, profile);
    }
    image_runs_.clear();
    for (const render_tree_bucket& bucket : far_selected_) {
        if (bucket.species < seasons_.size()) {
            image_runs_.push_back({seasons_[bucket.species].look * static_cast<std::size_t>(tree_variants) +
                                       static_cast<std::size_t>(bucket.variant),
                                   bucket.first, bucket.count});
        }
    }
    images_.draw(image_runs_, instance_vbo_, view_proj, eye.position, clock_seconds, wind, profile);
    return true;
}

void tree_renderer::draw_falling_leaves(const glm::mat4& view_proj, const tree_eye& eye, const float clock_seconds,
                                        const glm::vec2& wind, frame_profile* profile) {
    leaf_runs_.clear();
    std::size_t trees = 0;
    for (const render_tree_bucket& bucket : selected_) {
        trees += bucket.count;
        if (bucket.species >= seasons_.size() || seasons_[bucket.species].falling <= 0.0f) {
            continue;
        }
        const species_season& season = seasons_[bucket.species];
        const tree_look& look = looks_[season.look];
        leaf_runs_.push_back({bucket.first, bucket.count, tree_crown_box(look), season.falling, season.falling_withered,
                              season.leaves, look.withered});
    }
    leaves_.draw(leaf_runs_, instance_vbo_, largest_tree_, trees, view_proj, eye.position, eye.focal_pixels, clock_seconds, wind,
                 profile);
}

void tree_renderer::draw_casters(const shader_program& depth_shader, const glm::mat4& view_proj, frame_profile* profile) {
    if (index_.empty()) {
        return;
    }
    const view_frustum frustum = make_view_frustum(view_proj);
    index_.select(&frustum, nullptr, selected_, far_selected_);
    depth_shader.set_mat4("u_view_proj", view_proj);
    depth_shader.set_float("u_time", seconds_);
    depth_shader.set_vec2("u_wind", wind_);
    draw_selected(profile, true, [&](const species_season& season) { set_card_uniforms(depth_shader, season); });
}
