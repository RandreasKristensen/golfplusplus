#include "water_check.h"
#include "water_scene_check.h"

#include "core/sky_frame.h"
#include "renderer/backdrop_pass.h"
#include "renderer/gl_loader.h"
#include "renderer/gl_timer.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/water_renderer.h"
#include "renderer/reflection_pass.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr int width = 320;
constexpr int height = 240;
constexpr float fov_degrees = 50.0f;
constexpr float eye_height = 1.7f;
constexpr float pond_half_side = 3000.0f;
// The distant water checked: from this far below the horizon to this far,
// in degrees (about 650 m to 30 m away).
constexpr float nearest_degrees = 3.2f;
constexpr float farthest_degrees = 0.15f;
// No surface reflects more than all the light, so water brighter than what
// it mirrors (away from the sun's glint) fails, but for the water's own
// light from below, which shows only under a dark sky.
constexpr float brighter_share = 1.0f;
constexpr float own_light = 0.005f;
// A calm pond mirrors at least this share of its sky there; wind's waves,
// unresolved that far off, take at least a tenth of it.
constexpr float calm_mirror_share = 0.5f;
constexpr float windy_share_of_calm = 0.9f;

struct water_case {
    const char* name;
    float hour;
    float wind;       // m/s
    float cloud;      // shown_weather::cloud
    bool winter;      // on the winter solstice, for a true night
};

constexpr water_case cases[] = {
    {"clear_calm", 12.0f, 0.0f, 0.0f, false},   {"clear_windy", 12.0f, 8.0f, 0.0f, false},
    {"overcast_windy", 12.0f, 6.0f, 1.0f, false}, {"low_sun", 19.5f, 3.0f, 0.0f, false},
    {"night", 23.0f, 3.0f, 0.0f, true},
};
constexpr std::int64_t winter_solstice_1989 = 7294;  // days since 1970-01-01

float brightness(const glm::vec4& color) {
    return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

// The rows, from the bottom, `degrees` below the horizon.
int row_below(const float degrees) {
    const float y = -std::tan(glm::radians(degrees)) / std::tan(glm::radians(0.5f * fov_degrees));
    return std::clamp(static_cast<int>(std::floor((y * 0.5f + 0.5f) * static_cast<float>(height))), 0, height - 1);
}
}

water_check_result check_water(const check_course& course, const lighting_palette& palette, const std::string& asset_root,
                               const std::filesystem::path& out) {
    water_check_result result;
    const auto path = [&](const char* name) { return check_shader_path(asset_root, name); };
    GLuint quad_vao = 0;
    GLuint quad_vbo = 0;
    const float quad[] = {-1, -1, 0, 1, -1, 0, 1, 1, 0, -1, -1, 0, 1, 1, 0, -1, 1, 0};
    glGenVertexArrays(1, &quad_vao);
    glGenBuffers(1, &quad_vbo);
    glBindVertexArray(quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
    glBindVertexArray(0);
    backdrop_pass backdrop;
    water_renderer water;
    reflection_pass reflections;
    lighting_uniforms lighting;
    framebuffer scene;
    gl_timer_pool timers;
    timers.init();
    if (!backdrop.init(asset_root, path("backdrop.vert"), path("backdrop.frag"), quad_vao, palette.sky) ||
        !water.init(path("water.vert"), path("water.frag"), path("underwater.vert"), path("underwater.frag"), quad_vao) ||
        !lighting.init() ||
        !scene.init(width, height, framebuffer_format{framebuffer_color::hdr, framebuffer_depth::sampled, false, false})) {
        std::fprintf(stderr, "water check: cannot set up GL\n");
        return result;
    }
    result.ran = true;
    render_water pond;
    pond.revision = 1;
    for (const glm::vec2 corner : {glm::vec2(-1, -1), glm::vec2(1, -1), glm::vec2(1, 1), glm::vec2(-1, -1), glm::vec2(1, 1),
                                   glm::vec2(-1, 1)}) {
        render_water_vertex vertex;
        vertex.position = glm::vec3(corner.x, 0.0f, corner.y) * pond_half_side;
        std::fill(std::begin(vertex.fetch), std::end(vertex.fetch), water_fetch_cap_meters);
        pond.triangles.push_back(vertex);
    }
    const glm::vec3 eye(0.0f, eye_height, 0.0f);
    float calm_distant = 0.0f;
    for (const water_case& shown : cases) {
        sky_controls sky;
        sky.pinned_hour = shown.hour;
        if (shown.winter) {
            sky.pinned_date = winter_solstice_1989;
        }
        shown_weather weather;
        weather.cloud = shown.cloud;
        const render_lighting light = make_render_lighting(course.game, palette, sky, 0, weather);
        const glm::vec2 sun(light.sun_direction.x, light.sun_direction.z);
        const glm::vec2 away = glm::length(sun) > 1.0e-3f ? -glm::normalize(sun) : glm::vec2(0.0f, 1.0f);
        const glm::mat4 view = glm::lookAt(eye, eye + glm::vec3(away.x, 0.0f, away.y), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 proj = glm::perspective(glm::radians(fov_degrees), static_cast<float>(width) / height, 0.1f, 8000.0f);
        const scene_haze haze = backdrop.prepare(&course.game.place.backdrop, eye, course.game.area.extent, light.haze_tint);
        lighting.upload(light, haze, {}, nullptr);
        render_weather surface;
        surface.surface_wind = glm::vec3(shown.wind, 0.0f, 0.0f);
        timers.begin_frame(false);
        scene.bind();
        glViewport(0, 0, width, height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        backdrop.draw(view, proj, 3.0f, render_lightning{});
        reflections.draw(&pond, view, proj, eye, timers, [&](const water_reflection_view& reflected) {
            scene_haze mirrored_haze = backdrop.prepare(&course.game.place.backdrop, reflected.eye,
                                                              course.game.area.extent, light.haze_tint);
            mirrored_haze.reflection_level = reflected.level;
            lighting.upload(light, mirrored_haze, {}, nullptr);
            backdrop.draw(reflected.view, proj, 3.0f, render_lightning{}, true);
        });
        lighting.upload(light, haze, {}, nullptr);
        water.draw_surfaces(&pond, proj * view, palette.water, surface, reflections.surfaces(), 3.0f, timers, nullptr);
        const std::vector<glm::vec4> picture = read_colors(scene, width, height);
        ++result.pictures;

        // Each distant row against its mirror above the horizon, over the
        // middle of the picture.
        float water_sum = 0.0f;
        float mirror_sum = 0.0f;
        float distant_sum = 0.0f;
        int distant_count = 0;
        bool finite = true;
        for (int row = row_below(nearest_degrees); row <= row_below(farthest_degrees); ++row) {
            const int mirror = height - 1 - row;
            for (int column = width / 5; column < width - width / 5; ++column) {
                const glm::vec4& below = picture[static_cast<std::size_t>(row * width + column)];
                const glm::vec4& above = picture[static_cast<std::size_t>(mirror * width + column)];
                finite = finite && std::isfinite(below.r) && std::isfinite(below.g) && std::isfinite(below.b);
                water_sum += brightness(below);
                mirror_sum += brightness(above);
                if (row >= row_below(1.0f)) {
                    distant_sum += brightness(below);
                    ++distant_count;
                }
            }
        }
        const float distant = distant_count > 0 ? distant_sum / static_cast<float>(distant_count) : 0.0f;
        const float share = mirror_sum > 0.0f ? water_sum / mirror_sum : 0.0f;
        const int samples = (row_below(farthest_degrees) - row_below(nearest_degrees) + 1) * (width - 2 * (width / 5));
        const bool brighter = water_sum > brighter_share * mirror_sum + own_light * static_cast<float>(samples);
        bool ok = finite && !brighter;
        const char* why = !finite ? "not a number" : brighter ? "brighter than the sky it mirrors" : "";
        if (std::string(shown.name) == "clear_calm") {
            calm_distant = distant;
            if (share < calm_mirror_share) {
                ok = false;
                why = "a calm pond no longer mirrors its sky";
            }
        } else if (std::string(shown.name) == "clear_windy" && distant > windy_share_of_calm * calm_distant) {
            ok = false;
            why = "wind leaves the distant water as bright as calm";
        }
        result.failures += ok ? 0 : 1;
        std::printf("  water %-15s water/mirrored %.2f, distant %.3f%s%s\n", shown.name, share, distant, ok ? "" : ": FAILED, ", why);

        std::vector<std::array<std::uint8_t, 3>> look(static_cast<std::size_t>(width * height));
        for (std::size_t i = 0; i < look.size(); ++i) {
            for (int c = 0; c < 3; ++c) {
                look[i][static_cast<std::size_t>(c)] =
                    static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(picture[i][c], 0.0f, 1.0f), 1.0f / 2.2f)));
            }
        }
        write_check_bmp(out / (std::string("water_") + shown.name + ".bmp"), width, height, look);
    }
    result.failures += check_water_scene(course,palette,asset_root,out,backdrop,water,lighting,scene,timers) ? 0:1;
    ++result.pictures;
    framebuffer::bind_default();
    scene.shutdown();
    lighting.shutdown();
    water.shutdown();
    reflections.shutdown();
    backdrop.shutdown();
    timers.shutdown();
    glDeleteBuffers(1, &quad_vbo);
    glDeleteVertexArrays(1, &quad_vao);
    return result;
}
