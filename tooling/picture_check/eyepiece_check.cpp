#include "eyepiece_check.h"

#include "core/render_frame.h"
#include "game/rangefinder.h"
#include "game/sky_clock.h"
#include "picture_rig.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>

namespace {
constexpr int check_lines = 360;
constexpr int check_window_height = 1080;
constexpr float fov_degrees = 60.0f;
constexpr int headings = 4;
constexpr float pond_distance_meters = 25.0f;
constexpr float wind_meters_per_second = 6.0f;

struct condition {
    const char* name;
    float hour;
    bool winter;
};
// The middle of the course's largest pond, if it has one.
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

const std::array<condition, 2> conditions{{{"noon", 13.0f, false}, {"night", 24.0f, true}}};

bool write_picture(const std::filesystem::path& path, const std::vector<glm::vec4>& pixels, const int width, const int height) {
    std::vector<std::array<std::uint8_t, 3>> rgb;
    rgb.reserve(pixels.size());
    const auto byte_of = [](const float value) {
        return static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(value, 0.0f, 1.0f), 1.0f / 2.2f)));
    };
    for (const glm::vec4& pixel : pixels) { rgb.push_back({byte_of(pixel.r), byte_of(pixel.g), byte_of(pixel.b)}); }
    return write_check_bmp(path, width, height, rgb);
}
}

eyepiece_check_result check_eyepiece(const check_course& course, const game_content& content, const lighting_palette& palette,
                                     const std::string& asset_root, SDL_Window* window, const std::filesystem::path& out) {
    eyepiece_check_result result;
    picture_rig rig;
    if (!rig.init(window, course, content, palette, asset_root, check_window_height, check_lines)) { return result; }
    result.ran = true;
    const game_state& game = course.game;
    const glm::vec3 eye = game.player.position + glm::vec3(0.0f, 1.7f, 0.0f);
    const std::optional<glm::vec3> pond = largest_pond(rig.water());
    for (const condition& when : conditions) {
        rig.set_conditions(when.hour, when.winter, shown_target(game.weather));
        for (const bool ponds : {false, true}) {
            // The start looking four ways, then (with water) the pond from four banks, where the picture's middle
            // is water and its edge is not.
            for (int heading = 0; heading < (ponds && pond ? 2 * headings : headings); ++heading) {
                const glm::vec3 way = yaw_direction(game.player.yaw + glm::half_pi<float>() * static_cast<float>(heading));
                rig_camera camera;
                camera.eye = eye;
                camera.target = eye + way * 60.0f - glm::vec3(0.0f, 1.0f, 0.0f);
                if (heading >= headings) {
                    camera.eye = anchor_on_terrain(game.area, *pond - way * pond_distance_meters) + glm::vec3(0.0f, 1.7f, 0.0f);
                    camera.target = *pond;
                }
                camera.fov_degrees = fov_degrees;
                camera.clock_seconds = 3.0f;
                camera.wind = glm::vec2(wind_meters_per_second, 0.0f);
                camera.trees = true;
                camera.ponds = ponds;
                camera.magnification = rangefinder_magnification(game);
                rig_capture shaded;
                rig_capture trimmed;
                // A new pose's grass tiles are fetched over the first frames.
                rig.mask_eyepiece(false);
                for (int frame = 0; frame < 4; ++frame) { shaded = rig.capture(camera); }
                rig.mask_eyepiece(true);
                trimmed = rig.capture(camera);
                const std::string name = std::string(when.name) + (ponds ? "_ponds_" : "_dry_") + std::to_string(heading);
                ++result.pictures;
                if (shaded.eyepiece.empty() || shaded.eyepiece != trimmed.eyepiece) {
                    std::size_t differing = 0;
                    for (std::size_t i = 0; i < std::min(shaded.eyepiece.size(), trimmed.eyepiece.size()); ++i) {
                        differing += shaded.eyepiece[i] != trimmed.eyepiece[i] ? 1U : 0U;
                    }
                    std::printf("  eyepiece %s: %zu pixels differ\n", name.c_str(), differing);
                    ++result.failures;
                    write_picture(out / ("eyepiece_" + name + "_shaded.bmp"), shaded.eyepiece, shaded.scene_width, shaded.scene_height);
                    write_picture(out / ("eyepiece_" + name + "_trimmed.bmp"), trimmed.eyepiece, trimmed.scene_width, trimmed.scene_height);
                }
            }
        }
    }
    rig.shutdown();
    return result;
}
