#include "terrain_lod_check.h"

#include "core/weather_frame.h"
#include "picture_rig.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr int check_lines = 360;
constexpr int check_window_height = 1080;
constexpr float fov_degrees = 60.0f;
constexpr float wind_meters_per_second = 6.0f;
constexpr float loosest_error_pixels = 8.0f;
constexpr std::array<float, 2> pond_distances{25.0f, 70.0f};
// A step in any channel the eye would see, and the share of the picture that may take it.
constexpr float visible_delta = 0.02f;
constexpr float allowed_share = 0.01f;

std::optional<glm::vec3> largest_pond(const render_water& water) {
    std::optional<glm::vec3> pond;
    float largest = 0.0f;
    for (std::size_t i = 2; i < water.triangles.size(); i += 3) {
        const glm::vec3 a = water.triangles[i - 2].position;
        const glm::vec3 b = water.triangles[i - 1].position;
        const glm::vec3 c = water.triangles[i].position;
        const float area = std::abs(glm::cross(b - a, c - a).y);
        if (area > largest) {
            largest = area;
            pond = (a + b + c) / 3.0f;
        }
    }
    return pond;
}

float differing_share(const std::vector<glm::vec4>& a, const std::vector<glm::vec4>& b) {
    if (a.size() != b.size() || a.empty()) { return 1.0f; }
    std::size_t differing = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const glm::vec3 d = glm::abs(glm::vec3(a[i]) - glm::vec3(b[i]));
        differing += std::max(d.x, std::max(d.y, d.z)) > visible_delta ? 1U : 0U;
    }
    return static_cast<float>(differing) / static_cast<float>(a.size());
}

void write_picture(const std::filesystem::path& path, const std::vector<glm::vec4>& pixels, const int width, const int height) {
    std::vector<std::array<std::uint8_t, 3>> rgb;
    const auto byte_of = [](const float value) {
        return static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f)));
    };
    for (const glm::vec4& pixel : pixels) { rgb.push_back({byte_of(pixel.r), byte_of(pixel.g), byte_of(pixel.b)}); }
    write_check_bmp(path, width, height, rgb);
}
}

terrain_lod_check_result check_terrain_lod(const check_course& course, const game_content& content, const lighting_palette& palette,
                                           const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out) {
    terrain_lod_check_result result;
    if (course.terrain.lod_error_meters.empty() || palette.terrain_lod.mirror_max_error_pixels <= 0.0f) { return result; }
    lighting_palette full = palette;
    full.terrain_lod.mirror_max_error_pixels = 0.0f;
    lighting_palette loosest = palette;
    loosest.terrain_lod.mirror_max_error_pixels = loosest_error_pixels;
    picture_rig oracle;
    picture_rig shipped;
    picture_rig coarse;
    const auto shut_down = [&] {
        oracle.shutdown();
        shipped.shutdown();
        coarse.shutdown();
    };
    if (!oracle.init(window, course, content, full, asset_root, check_window_height, check_lines) ||
        !shipped.init(window, course, content, palette, asset_root, check_window_height, check_lines) ||
        !coarse.init(window, course, content, loosest, asset_root, check_window_height, check_lines)) {
        shut_down();
        return result;
    }
    const std::optional<glm::vec3> pond = largest_pond(oracle.water());
    if (!pond) {
        shut_down();
        return result;
    }
    result.ran = true;
    const game_state& game = course.game;
    float worst = 0.0f;
    float coarsest = 0.0f;
    for (const float distance : pond_distances) {
        for (int heading = 0; heading < 4; ++heading) {
            const glm::vec3 way = yaw_direction(game.player.yaw + glm::half_pi<float>() * static_cast<float>(heading));
            rig_camera camera;
            camera.eye = anchor_on_terrain(game.area, *pond - way * distance) + glm::vec3(0.0f, 1.7f, 0.0f);
            camera.target = *pond;
            camera.fov_degrees = fov_degrees;
            camera.clock_seconds = 3.0f;
            camera.wind = glm::vec2(wind_meters_per_second, 0.0f);
            camera.trees = true;
            camera.ponds = true;
            rig_capture pictures[3];
            picture_rig* rigs[3] = {&oracle, &shipped, &coarse};
            for (int i = 0; i < 3; ++i) {
                rigs[i]->set_conditions(13.0f, false, shown_target(game.weather));
                for (int frame = 0; frame < 4; ++frame) { pictures[i] = rigs[i]->capture(camera); }
            }
            const float share = differing_share(pictures[0].scene, pictures[1].scene);
            worst = std::max(worst, share);
            coarsest = std::max(coarsest, differing_share(pictures[0].scene, pictures[2].scene));
            ++result.pictures;
            if (share > allowed_share) {
                const std::string name = std::to_string(static_cast<int>(distance)) + "_" + std::to_string(heading);
                std::printf("  terrain levels %s: %.3f%% of the pixels differ from the full ground\n", name.c_str(), 100.0 * static_cast<double>(share));
                write_picture(out / ("terrain_lod_" + name + "_full.bmp"), pictures[0].scene, pictures[0].scene_width, pictures[0].scene_height);
                write_picture(out / ("terrain_lod_" + name + "_levels.bmp"), pictures[1].scene, pictures[1].scene_width, pictures[1].scene_height);
                ++result.failures;
            }
        }
    }
    const bool sees_coarse = coarsest > allowed_share;
    std::printf("  terrain levels: %d pictures, at most %.3f%% differ (limit %.1f%%); at %.0f pixels error %.3f%% do%s\n", result.pictures,
                100.0 * static_cast<double>(worst), 100.0 * static_cast<double>(allowed_share), static_cast<double>(loosest_error_pixels),
                100.0 * static_cast<double>(coarsest), sees_coarse ? "" : ": the check cannot see a coarse level FAILED");
    result.failures += sees_coarse ? 0 : 1;
    shut_down();
    return result;
}
