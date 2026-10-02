#include "renderer/backdrop_pass.h"

#include "renderer/gl_loader.h"

#include <filesystem>
#include <optional>

#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

namespace {
constexpr int panorama_unit = 0;
constexpr GLsizei screen_quad_vertices = 6;
}

bool backdrop_pass::init(const std::string& asset_root,
                         const std::string& vertex_path,
                         const std::string& fragment_path,
                         const unsigned int screen_vao) {
    asset_root_ = asset_root;
    screen_vao_ = screen_vao;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    shader_.use();
    shader_.set_int("u_panorama", panorama_unit);
    return true;
}

void backdrop_pass::shutdown() {
    panorama_.shutdown();
    shader_.shutdown();
    panorama_image_.clear();
}

void backdrop_pass::draw(const std::string& image, const glm::mat4& view, const glm::mat4& proj) {
    if (image != panorama_image_) {
        panorama_image_ = image;
        panorama_.shutdown();
        if (!image.empty()) {
            const std::optional<rgb_image> loaded = load_bmp_file(std::filesystem::path(asset_root_) / image);
            if (!loaded || !panorama_.upload(*loaded, texture_sampling::panorama)) {
                SDL_Log("Backdrop %s is missing or not an uncompressed 24/32-bit BMP", image.c_str());
            }
        }
    }
    if (!panorama_.loaded()) {
        return;
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    shader_.use();
    shader_.set_mat4("u_inverse_view_proj", glm::inverse(proj * glm::mat4(glm::mat3(view))));
    panorama_.bind(panorama_unit);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}
