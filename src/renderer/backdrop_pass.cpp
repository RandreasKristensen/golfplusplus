#include "renderer/backdrop_pass.h"

#include "renderer/gl_loader.h"

#include <cstddef>
#include <filesystem>
#include <optional>

#include <glm/mat3x3.hpp>
#include <glm/trigonometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr int sky_unit = 0;
constexpr int land_unit = 2;
constexpr GLsizei screen_quad_vertices = 6;

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

// The mean brightness of `image`, as backdrop.frag measures it.
float mean_brightness(const rgba_image& image) {
    double total = 0.0;
    const std::size_t count = image.pixels.size() / 4U;
    for (std::size_t i = 0; i < count; ++i) {
        total += (0.2126 * image.pixels[i * 4U] + 0.7152 * image.pixels[i * 4U + 1U] + 0.0722 * image.pixels[i * 4U + 2U]) / 255.0;
    }
    return count > 0 ? static_cast<float>(total / static_cast<double>(count)) : 0.0f;
}
}

bool backdrop_pass::init(const std::string& asset_root,
                         const std::string& vertex_path,
                         const std::string& fragment_path,
                         const unsigned int screen_vao,
                         const sky_style& sky) {
    asset_root_ = asset_root;
    screen_vao_ = screen_vao;
    if (!shader_.load_from_files(vertex_path, fragment_path)) {
        return false;
    }
    shader_.use();
    shader_.set_int("u_sky", sky_unit);
    shader_.set_int("u_land", land_unit);
    shader_.set_float("u_sun_radius", glm::radians(sky.sun_radius_deg));
    shader_.set_float("u_sun_brightness", sky.sun_brightness);
    shader_.set_float("u_sun_halo_radius", glm::radians(sky.sun_halo_radius_deg));
    shader_.set_float("u_sun_halo_strength", sky.sun_halo_strength);
    shader_.set_float("u_moon_radius", glm::radians(sky.moon_radius_deg));
    shader_.set_vec3("u_moon_color", sky.moon_color);
    shader_.set_float("u_moon_earthshine", sky.moon_earthshine);
    shader_.set_float("u_glow_width", glm::radians(sky.glow_width_deg));
    shader_.set_float("u_glow_height", glm::radians(sky.glow_height_deg));
    shader_.set_float("u_star_spacing", glm::radians(sky.star_spacing_deg));
    shader_.set_float("u_star_density", sky.star_density);
    shader_.set_float("u_star_radius", glm::radians(sky.star_radius_deg));
    shader_.set_float("u_star_brightness", sky.star_brightness);
    shader_.set_float("u_star_twinkle", sky.star_twinkle);
    shader_.set_float("u_star_twinkle_hz", sky.star_twinkle_hz);
    shader_.set_float("u_star_horizon_fade", glm::radians(sky.star_horizon_fade_deg));
    shader_.set_float("u_cloud_threshold", sky.cloud_threshold);
    shader_.set_float("u_cloud_softness", sky.cloud_softness);
    shader_.set_float("u_cloud_sun_gain", sky.cloud_sun_gain);
    shader_.set_float("u_cloud_sun_width", glm::radians(sky.cloud_sun_width_deg));
    shader_.set_vec3("u_cloud_shade", sky.cloud_shade);
    shader_.set_vec4("u_cloud_deck", glm::vec4(sky.cloud_deck_scale, sky.cloud_deck_drift, sky.cloud_deck_clear, sky.cloud_deck_full));
    shader_.set_vec4("u_cloud_deck_look", glm::vec4(sky.cloud_deck_softness, sky.cloud_deck_top, sky.cloud_deck_base, 0.0f));
    shader_.set_vec4("u_rainbow", glm::vec4(glm::radians(sky.rainbow_radius_deg), glm::radians(sky.rainbow_width_deg),
                                            glm::radians(sky.rainbow_secondary_radius_deg),
                                            glm::radians(sky.rainbow_secondary_width_deg)));
    shader_.set_vec4("u_rainbow_look", glm::vec4(sky.rainbow_secondary_share, sky.rainbow_brightness, sky.rainbow_inner_glow,
                                                 sky.rainbow_band_darkening));
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
    sky_brightness_ = 0.0f;
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
    sky_brightness_ = mean_brightness(*sky);
}

scene_haze backdrop_pass::prepare(const course_backdrop* backdrop, const glm::vec3& eye, const float area_extent,
                                  const glm::vec3& haze_tint) {
    const course_backdrop none;
    const course_backdrop& shown = backdrop != nullptr ? *backdrop : none;
    if (shown.sky != sky_image_ || shown.land != land_image_) {
        load(shown);
    }
    haze_amount_ = shown.haze_amount;
    return sky_.loaded() && land_.loaded() ? make_scene_haze(shown, far_ground_haze_, eye, area_extent, haze_tint) : scene_haze{};
}

void backdrop_pass::draw(const glm::mat4& view, const glm::mat4& proj, const float seconds, const render_lightning& lightning) {
    if (!sky_.loaded() || !land_.loaded()) {
        return;
    }
    // The quad sits on the far plane: it passes only where the depth buffer
    // is still clear.
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    shader_.use();
    shader_.set_mat4("u_inverse_view_proj", glm::inverse(proj * glm::mat4(glm::mat3(view))));
    shader_.set_float("u_haze_amount", haze_amount_);
    shader_.set_float("u_sky_brightness", sky_brightness_);
    shader_.set_float("u_seconds", seconds);
    shader_.set_vec4("u_lightning", glm::vec4(lightning.direction, lightning.flash));
    shader_.set_vec4("u_lightning_bolt", glm::vec4(lightning.seed, lightning.bolt ? lightning.bolt_brightness : 0.0f,
                                                   lightning.bolt_width, lightning.bolt_jitter));
    shader_.set_vec4("u_lightning_glow", glm::vec4(lightning.color, lightning.glow_strength));
    shader_.set_float("u_lightning_glow_radius", lightning.glow_radius);
    sky_.bind(sky_unit);
    land_.bind(land_unit);
    glBindVertexArray(screen_vao_);
    glDrawArrays(GL_TRIANGLES, 0, screen_quad_vertices);
    record_draw_call(shader_.profile());
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
}
