#include "renderer/tree_impostor_renderer.h"

#include <SDL.h>

#include <array>
#include <cmath>
#include <filesystem>

#include <glm/common.hpp>
#include <glm/vec4.hpp>

#include "renderer/gl_loader.h"
#include "renderer/render_tree.h"

namespace {
constexpr int image_unit = 6;  // albedo, normal, leaf: units 6, 7, 8, bound for each draw
constexpr int strip_rows = 9;  // rows of a strip: the pose bends the crown with the square of its height
constexpr GLuint corner_location = 0;
constexpr GLuint instance_offset_location = 3;
constexpr GLuint instance_scale_location = 4;
constexpr GLuint instance_turn_location = 5;
constexpr float pi = 3.14159265f;

void filter(const unsigned texture) {
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}
}

void tree_impostor_renderer::set_grid(const shader_program& program) const {
    std::array<glm::vec4, 16> azimuths{};
    for (int i = 0; i < style_.azimuths; ++i) {
        const float angle = 2.0f * pi * static_cast<float>(i) / static_cast<float>(style_.azimuths);
        azimuths[static_cast<std::size_t>(i)] = glm::vec4(std::cos(angle), std::sin(angle), 0.0f, 0.0f);
    }
    std::array<glm::vec4, 8> elevations{};
    for (std::size_t j = 0; j < style_.elevations_degrees.size(); ++j) {
        const float angle = style_.elevations_degrees[j] * pi / 180.0f;
        elevations[j] = glm::vec4(std::cos(angle), std::sin(angle), angle, 0.0f);
    }
    program.set_int("u_azimuths", style_.azimuths);
    program.set_int("u_elevations", static_cast<int>(style_.elevations_degrees.size()));
    program.set_vec4_array("u_azimuth_table", azimuths.data(), 16);
    program.set_vec4_array("u_elevation_table", elevations.data(), 8);
}

void tree_impostor_renderer::set_box(const shader_program& program, const tree_box& box) {
    program.set_vec3("u_box_centre", box.centre);
    program.set_vec3("u_box_half", box.half);
}

bool tree_impostor_renderer::init(const std::string& bake_vertex, const std::string& bake_fragment,
                                  const std::string& image_vertex, const std::string& image_fragment,
                                  const tree_impostor_style& style, const std::size_t models) {
    shutdown();
    style_ = style;
    models_.assign(models, model_images{});
    const std::filesystem::path directory = std::filesystem::path(bake_vertex).parent_path();
    if (!bake_shader_.load_from_files(bake_vertex, bake_fragment) || !shader_.load_from_files(image_vertex, image_fragment) ||
        !resolve_shader_.load_from_files((directory / "tree_bake_resolve.vert").string(),
                                         (directory / "tree_bake_resolve.frag").string())) {
        shutdown();
        return false;
    }
    const int layers = static_cast<int>(models) * views_per_model();
    unsigned int textures[3] = {};
    glGenTextures(3, textures);
    albedo_ = textures[0];
    normal_ = textures[1];
    leaf_ = textures[2];
    glGenFramebuffers(1, &framebuffer_);
    glGenFramebuffers(1, &fine_framebuffer_);
    glGenTextures(3, fine_);
    glGenRenderbuffers(1, &fine_depth_);
    glGenVertexArrays(1, &resolve_vao_);
    glGenVertexArrays(1, &strip_vao_);
    glGenBuffers(1, &strip_vbo_);
    if (!albedo_ || !normal_ || !leaf_ || !framebuffer_ || !fine_framebuffer_ || !fine_[0] || !fine_[1] || !fine_[2] ||
        !fine_depth_ || !resolve_vao_ || !strip_vao_ || !strip_vbo_ || layers <= 0) {
        SDL_Log("Tree images failed to allocate GL objects.");
        shutdown();
        return false;
    }
    const int tile = style_.tile_pixels;
    const unsigned int formats[3] = {GL_RGBA8, GL_RGBA8, GL_RG8};
    const unsigned int pixel_formats[3] = {GL_RGBA, GL_RGBA, GL_RG};
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, textures[i]);
        storage_.texture_array(textures[i], formats[i], tile, tile, layers, pixel_formats[i], GL_UNSIGNED_BYTE, true);
        filter(textures[i]);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    const int fine = tile * bake_supersample;
    for (int i = 0; i < 3; ++i) {
        glBindTexture(GL_TEXTURE_2D, fine_[i]);
        storage_.texture_image(fine_[i], formats[i], fine, fine, pixel_formats[i], GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, fine_depth_);
    storage_.depth_stencil(fine_depth_, fine, fine);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, fine_framebuffer_);
    for (int i = 0; i < 3; ++i) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, static_cast<GLenum>(GL_COLOR_ATTACHMENT0 + i), GL_TEXTURE_2D, fine_[i], 0);
    }
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, fine_depth_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    texture_bytes_ = 0;
    for (int side = tile; side >= 1; side /= 2) {
        texture_bytes_ += static_cast<std::size_t>(side) * static_cast<std::size_t>(side) * 10U * static_cast<std::size_t>(layers);
    }

    // A strip of rows across the picture: x is the side, y the height, both -1..1.
    std::vector<glm::vec2> corners;
    for (int row = 0; row < strip_rows; ++row) {
        const float y = 2.0f * static_cast<float>(row) / static_cast<float>(strip_rows - 1) - 1.0f;
        corners.push_back(glm::vec2(-1.0f, y));
        corners.push_back(glm::vec2(1.0f, y));
    }
    glBindVertexArray(strip_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, strip_vbo_);
    storage_.buffer(strip_vbo_, GL_ARRAY_BUFFER, corners.size() * sizeof(glm::vec2), corners.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(corner_location);
    glVertexAttribPointer(corner_location, 2, GL_FLOAT, GL_FALSE, sizeof(glm::vec2), nullptr);
    for (const GLuint location : {instance_offset_location, instance_scale_location, instance_turn_location}) {
        glEnableVertexAttribArray(location);
        glVertexAttribDivisor(location, 1);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader_.use();
    shader_.set_int("u_albedo", image_unit);
    shader_.set_int("u_normal", image_unit + 1);
    shader_.set_int("u_leaf", image_unit + 2);
    resolve_shader_.use();
    resolve_shader_.set_int("u_albedo", image_unit);
    resolve_shader_.set_int("u_normal", image_unit + 1);
    resolve_shader_.set_int("u_leaf", image_unit + 2);
    resolve_shader_.set_int("u_scale", bake_supersample);
    return true;
}

void tree_impostor_renderer::shutdown() {
    storage_.delete_texture(albedo_);
    storage_.delete_texture(normal_);
    storage_.delete_texture(leaf_);
    for (unsigned int& texture : fine_) {
        storage_.delete_texture(texture);
    }
    storage_.delete_renderbuffer(fine_depth_);
    storage_.delete_buffer(strip_vbo_);
    for (unsigned int* framebuffer : {&framebuffer_, &fine_framebuffer_}) {
        if (*framebuffer != 0) {
            glDeleteFramebuffers(1, framebuffer);
            *framebuffer = 0;
        }
    }
    if (resolve_vao_ != 0) {
        glDeleteVertexArrays(1, &resolve_vao_);
        resolve_vao_ = 0;
    }
    if (strip_vao_ != 0) {
        glDeleteVertexArrays(1, &strip_vao_);
        strip_vao_ = 0;
    }
    bake_shader_.shutdown();
    resolve_shader_.shutdown();
    shader_.shutdown();
    models_.clear();
    texture_bytes_ = 0;
}

void tree_impostor_renderer::forget_all() {
    models_.assign(models_.size(), model_images{});
}

void tree_impostor_renderer::begin_bake(frame_profile* profile) {
    record_state_readbacks(profile, 5);
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &saved_framebuffer_);
    glGetIntegerv(GL_VIEWPORT, saved_viewport_);
    saved_depth_test_ = glIsEnabled(GL_DEPTH_TEST) == GL_TRUE;
    saved_blend_ = glIsEnabled(GL_BLEND) == GL_TRUE;
    saved_scissor_ = glIsEnabled(GL_SCISSOR_TEST) == GL_TRUE;
    const GLenum targets[3] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2};
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glDrawBuffers(3, targets);
    glBindFramebuffer(GL_FRAMEBUFFER, fine_framebuffer_);
    glDrawBuffers(3, targets);
    bake_shader_.use();
    set_grid(bake_shader_);
    // The fine view is read on the images' own units; draw() binds the arrays there again.
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + image_unit + i));
        glBindTexture(GL_TEXTURE_2D, fine_[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glDepthMask(GL_TRUE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void tree_impostor_renderer::bake(const std::size_t model, const tree_image_source& source) {
    if (model >= models_.size() || source.vao == 0) {
        return;
    }
    bake_shader_.use();
    set_box(bake_shader_, source.box);
    bake_shader_.set_vec3("u_bark_color", source.bark);
    bake_shader_.set_vec3("u_twig_color", source.twig);
    bake_shader_.set_vec3("u_leaf_color", source.leaves);
    bake_shader_.set_vec3("u_withered_color", source.withered);
    bake_shader_.set_vec2("u_season", source.season);
    bake_shader_.set_vec4("u_twigs", source.twigs);
    const float clear[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const int fine = style_.tile_pixels * bake_supersample;
    for (int elevation = 0; elevation < static_cast<int>(style_.elevations_degrees.size()); ++elevation) {
        for (int azimuth = 0; azimuth < style_.azimuths; ++azimuth) {
            // The view, finely.
            glBindFramebuffer(GL_FRAMEBUFFER, fine_framebuffer_);
            glViewport(0, 0, fine, fine);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            for (int target = 0; target < 3; ++target) {
                glClearBufferfv(GL_COLOR, target, clear);
            }
            glClear(GL_DEPTH_BUFFER_BIT);
            bake_shader_.use();
            bake_shader_.set_int("u_view_azimuth", azimuth);
            bake_shader_.set_int("u_view_elevation", elevation);
            glBindVertexArray(source.vao);
            glDrawElementsInstanced(GL_TRIANGLES, source.index_count, GL_UNSIGNED_INT, nullptr, 1);
            // Averaged into its tile.
            const int layer = static_cast<int>(model) * views_per_model() + elevation * style_.azimuths + azimuth;
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, albedo_, 0, layer);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, normal_, 0, layer);
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, leaf_, 0, layer);
            glViewport(0, 0, style_.tile_pixels, style_.tile_pixels);
            glDisable(GL_DEPTH_TEST);
            resolve_shader_.use();
            glBindVertexArray(resolve_vao_);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }
    }
    glBindVertexArray(0);
    models_[model] = model_images{true, source.box, source.shine_through};
}

void tree_impostor_renderer::end_bake() {
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer_);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, 0, 0, 0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, 0, 0, 0);
    glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, 0, 0, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(saved_framebuffer_));
    glViewport(saved_viewport_[0], saved_viewport_[1], saved_viewport_[2], saved_viewport_[3]);
    if (saved_depth_test_) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    if (saved_blend_) {
        glEnable(GL_BLEND);
    }
    if (saved_scissor_) {
        glEnable(GL_SCISSOR_TEST);
    }
    for (const unsigned int texture : {albedo_, normal_, leaf_}) {
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture);
        glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    }
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void tree_impostor_renderer::draw(const std::vector<tree_image_run>& runs, const unsigned instance_buffer,
                                  const glm::mat4& view_proj, const glm::vec3& eye, const float seconds,
                                  const glm::vec2& wind, frame_profile* profile) {
    if (runs.empty() || shader_.id() == 0) {
        return;
    }
    shader_.set_profile(profile);
    shader_.use();
    const unsigned int textures[3] = {albedo_, normal_, leaf_};
    for (int i = 0; i < 3; ++i) {
        glActiveTexture(static_cast<GLenum>(GL_TEXTURE0 + image_unit + i));
        glBindTexture(GL_TEXTURE_2D_ARRAY, textures[i]);
    }
    glActiveTexture(GL_TEXTURE0);
    shader_.set_mat4("u_view_proj", view_proj);
    shader_.set_vec3("u_eye", eye);
    shader_.set_float("u_time", seconds);
    shader_.set_vec2("u_wind", wind);
    set_grid(shader_);
    glBindVertexArray(strip_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, instance_buffer);
    constexpr GLsizei stride = sizeof(render_tree_instance);
    int last_model = -1;
    for (const tree_image_run& run : runs) {
        if (!baked(run.model)) {
            continue;
        }
        const model_images& images = models_[run.model];
        if (static_cast<int>(run.model) != last_model) {
            set_box(shader_, images.box);
            shader_.set_int("u_first_layer", static_cast<int>(run.model) * views_per_model());
            shader_.set_float("u_shine_through", images.shine_through);
            const float cell = static_cast<float>(style_.tile_pixels) / (2.0f * glm::max(images.box.half.x, images.box.half.y));
            shader_.set_float("u_cell_scale", cell);
            last_model = static_cast<int>(run.model);
        }
        const std::size_t first = run.first * sizeof(render_tree_instance);
        glVertexAttribPointer(instance_offset_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, offset)));
        glVertexAttribPointer(instance_scale_location, 3, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, scale)));
        glVertexAttribPointer(instance_turn_location, 2, GL_FLOAT, GL_FALSE, stride,
                              reinterpret_cast<const void*>(first + offsetof(render_tree_instance, yaw)));
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 2 * strip_rows, static_cast<GLsizei>(run.count));
        if (profile) {
            profile->work.current().tree_images += run.count;
        }
        record_draw_call(profile);
    }
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}
