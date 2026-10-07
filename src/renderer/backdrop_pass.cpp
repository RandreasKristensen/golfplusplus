#include "renderer/backdrop_pass.h"

#include "renderer/gl_loader.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <optional>

#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>

namespace {
constexpr int sky_unit = 0;
constexpr int land_unit = 2;
constexpr GLsizei screen_quad_vertices = 6;
// Keeps the haze ramp from collapsing on a tiny play area.
constexpr float min_haze_distance = 1.0f;

// How hazy the land panorama's bottom row is: its alpha is how much of the
// land shows through the haze.
float bottom_row_haze(const rgba_image& land) {
    if (land.width <= 0) {
        return 0.0f;
    }
    float visibility = 0.0f;
    for (int column = 0; column < land.width; ++column) {
        visibility += static_cast<float>(land.pixels[static_cast<std::size_t>(column) * 4U + 3U]) / 255.0f;
    }
    return 1.0f - visibility / static_cast<float>(land.width);
}
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
    shader_.set_int("u_sky", sky_unit);
    shader_.set_int("u_land", land_unit);
    return true;
}

void backdrop_pass::shutdown() {
    sky_.shutdown();
    land_.shutdown();
    shader_.shutdown();
    sky_image_.clear();
    land_image_.clear();
}

void backdrop_pass::load(const course_backdrop& backdrop) {
    sky_image_ = backdrop.sky;
    land_image_ = backdrop.land;
    sky_.shutdown();
    land_.shutdown();
    far_ground_haze_ = 0.0f;
    if (backdrop.sky.empty() || backdrop.land.empty()) {
        return;
    }
    const std::filesystem::path root(asset_root_);
    const std::optional<rgba_image> sky = load_bmp_file(root / backdrop.sky);
    const std::optional<rgba_image> land = load_bmp_file(root / backdrop.land);
    if (!sky || !land || !sky_.upload(*sky, texture_sampling::panorama) || !land_.upload(*land, texture_sampling::panorama)) {
        SDL_Log("Backdrop %s or %s is missing or not an uncompressed 24/32-bit BMP", backdrop.sky.c_str(), backdrop.land.c_str());
        sky_.shutdown();
        land_.shutdown();
        return;
    }
    far_ground_haze_ = bottom_row_haze(*land);
}

scene_haze backdrop_pass::draw(const course_backdrop* backdrop, const glm::vec3& eye, const float area_extent,
                               const glm::mat4& view, const glm::mat4& proj) {
    const course_backdrop none;
    const course_backdrop& shown = backdrop != nullptr ? *backdrop : none;
    if (shown.sky != sky_image_ || shown.land != land_image_) {
        load(shown);
    }
    if (!sky_.loaded() || !land_.loaded()) {
        return scene_haze{};
    }

    scene_haze haze;
    haze.color = shown.haze_color;
    haze.far_amount = std::clamp(far_ground_haze_ * shown.haze_amount, 0.0f, 1.0f);
    // The drawn ground ends at least area_extent from anywhere in the play
    // area, so the haze is full by its edge. No play area (menus) keeps the
    // course's distance.
    const float distance = area_extent > 0.0f ? std::min(shown.haze_distance, area_extent) : shown.haze_distance;
    haze.full_distance = std::max(min_haze_distance, distance);
    haze.eye = eye;

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    shader_.use();
    shader_.set_mat4("u_inverse_view_proj", glm::inverse(proj * glm::mat4(glm::mat3(view))));
    shader_.set_vec3("u_haze_color", haze.color);
    shader_.set_float("u_haze_amount", shown.haze_amount);
    sky_.bind(sky_unit);
    land_.bind(land_unit);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return haze;
}
