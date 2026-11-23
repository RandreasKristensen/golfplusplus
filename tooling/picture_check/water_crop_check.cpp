#include "water_crop_check.h"
#include "core/sky_frame.h"
#include "renderer/gl_loader.h"
#include "renderer/reflection_pass.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <tuple>
#include <vector>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr int width = 320;
constexpr int height = 240;
// Whole-picture and cropped mirrors agree up to the rasteriser's rounding at
// silhouettes: few pixels differ, none by much.
constexpr float visible_delta = 0.02f;
constexpr float allowed_share = 0.0005f;
constexpr float allowed_peak = 0.25f;

struct view_case {
    const char* name;
    glm::vec3 eye;
    float yaw_degrees;    // 90 looks along +z
    float pitch_degrees;
    float wind;           // m/s
    float rain;
    bool steepest;        // the waves at the tuning's steepest (lighting.json's bounds)
};

constexpr view_case cases[] = {
    {"calm_ahead", {0, 1.7f, 0}, 90, -8, 0, 0, false},
    {"max_wind_ahead", {0, 1.7f, 0}, 90, -8, 25, 1, true},
    {"windy_down", {0, 1.7f, 0}, 90, -22, 8, 0, false},
    {"grazing", {0, 0.4f, 0}, 90, -1, 6, 0, true},
    {"high_above", {0, 25, 0}, 90, -30, 4, 0, false},
    {"corner_left", {-45, 1.7f, 10}, 30, -6, 8, 0, true},
    {"corner_right", {70, 1.7f, 0}, 150, -6, 8, 0, false},
    {"two_heights", {45, 2.0f, -10}, 110, -10, 3, 0, false},
    {"low_beside", {10, 0.3f, 10}, 90, -4, 25, 0, true},
    {"on_the_water", {0, 0.05f, 30}, 90, -5, 8, 0, true},
    {"under_water", {0, -0.5f, 30}, 90, 3, 8, 0, false},
    {"turn_a", {0, 1.7f, 0}, 40, -8, 8, 0, false},
    {"turn_b", {0, 1.7f, 0}, 70, -8, 8, 0, false},
    {"turn_c", {0, 1.7f, 0}, 100, -8, 8, 0, false},
    {"turn_d", {0, 1.7f, 0}, 130, -8, 8, 0, false},
    {"turn_e", {0, 1.7f, 0}, 160, -8, 8, 0, false},
};

glm::mat4 view_of(const view_case& shown) {
    const float yaw = glm::radians(shown.yaw_degrees);
    const float pitch = glm::radians(shown.pitch_degrees);
    const glm::vec3 forward(std::cos(yaw) * std::cos(pitch), std::sin(pitch), std::sin(yaw) * std::cos(pitch));
    return glm::lookAt(shown.eye, shown.eye + forward, glm::vec3(0, 1, 0));
}

// A mirror that is stripes two texels wide in red across and in green down, in
// the whole picture's texels, so a lookup a texel off its place shows at once.
void paint_stripes(const reflection_crop& crop) {
    glEnable(GL_SCISSOR_TEST);
    const auto stripe = [](const int texel) { return (texel / 2) % 2 == 0 ? 0.1f : 1.0f; };
    glColorMask(GL_TRUE, GL_FALSE, GL_FALSE, GL_FALSE);
    for (int column = 0; column < crop.width; ++column) {
        glScissor(column, 0, 1, crop.height);
        glClearColor(stripe(crop.x + column), 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glColorMask(GL_FALSE, GL_TRUE, GL_FALSE, GL_FALSE);
    for (int row = 0; row < crop.height; ++row) {
        glScissor(0, row, crop.width, 1);
        glClearColor(0, stripe(crop.y + row), 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glColorMask(GL_FALSE, GL_FALSE, GL_TRUE, GL_FALSE);
    glScissor(0, 0, crop.width, crop.height);
    glClearColor(0, 0, 0.5f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_SCISSOR_TEST);
}

render_water make_ponds() {
    render_water ponds;
    ponds.revision = 931;
    for (const auto& [level, low, high] : {std::tuple{0.0f, glm::vec2(-30, 15), glm::vec2(30, 70)},
                                           std::tuple{-1.2f, glm::vec2(40, -20), glm::vec2(100, 30)},
                                           std::tuple{0.0f, glm::vec2(-90, -10), glm::vec2(-50, 40)}}) {
        for (const glm::vec2 p : {glm::vec2(low.x, low.y), glm::vec2(high.x, low.y), glm::vec2(high.x, high.y),
                                  glm::vec2(low.x, low.y), glm::vec2(high.x, high.y), glm::vec2(low.x, high.y)}) {
            render_water_vertex v;
            v.position = {p.x, level, p.y};
            std::fill(std::begin(v.fetch), std::end(v.fetch), water_fetch_cap_meters);
            ponds.triangles.push_back(v);
        }
    }
    index_render_water(ponds);
    return ponds;
}
}

bool check_water_crops(const check_course& course, const lighting_palette& palette, const std::string& root,
                       const std::filesystem::path& out, backdrop_pass& backdrop, water_renderer& water,
                       lighting_uniforms& lighting, framebuffer& scene, gl_timer_pool& timers) {
    tree_renderer tree;
    if (!tree.init(check_shader_path(root, "tree.vert"), check_shader_path(root, "tree.frag"), palette.trees, palette.tree_impostor)) { return false; }
    tree.set_seasons({{palette.trees.front().name, 1, 0, 0}});
    std::vector<tree_body> trees;
    for (int i = 0; i < 40; ++i) {
        tree_body body;
        body.base = {-80.0f + 4.5f * static_cast<float>(i), (i % 5 == 0) ? 12.0f : (i % 3 == 0 ? 1.5f : 0.0f),
                     -25.0f + 13.0f * static_cast<float>((i * 7) % 11)};
        body.shape = {0.4f, 5, 4, 9};
        trees.push_back(body);
    }
    tree.set_trees(trees, 1, nullptr);
    sky_controls sky;
    sky.pinned_hour = 12;
    const render_lighting light = make_render_lighting(course.game, palette, sky, 0);
    const render_water ponds = make_ponds();
    const glm::mat4 proj = glm::perspective(glm::radians(55.0f), static_cast<float>(width) / height, 0.1f, 1500.0f);
    reflection_pass reflections;
    bool ok = true;
    std::uint64_t cropped_total = 0;
    std::uint64_t whole_total = 0;
    for (const bool striped : {false, true}) {
    for (const view_case& shown : cases) {
        const glm::mat4 view = view_of(shown);
        render_weather weather;
        weather.surface_wind = glm::vec3(shown.wind, 0, 0);
        weather.surface_rain = shown.rain;
        water_style style = palette.water;
        if (shown.steepest) {
            style.ripple_slope = 0.5f;
            style.fine_slope = 0.5f;
            style.rain_ring_slope = 1.0f;
        }
        std::vector<glm::vec4> pictures[2];
        std::uint64_t pixels[2] = {};
        for (const bool whole : {true, false}) {
            frame_profile profile;
            scene.bind();
            glViewport(0, 0, width, height);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            glEnable(GL_DEPTH_TEST);
            const scene_haze haze = backdrop.prepare(&course.game.place.backdrop, shown.eye, course.game.area.extent, light.haze_tint);
            lighting.upload(light, haze, {}, nullptr);
            backdrop.draw(view, proj, 3, {});
            tree.draw(proj * view, make_view_frustum(proj * view), 3, glm::vec2(0), nullptr);
            auto candidates = water.candidates(&ponds, proj * view, shown.eye, style, weather, &profile);
            if (whole) {
                for (water_candidate& candidate : candidates) { candidate.coverage = glm::vec4(0, 0, 1, 1); }
            }
            const draw_target target{scene.id(), width, height, false};
            reflections.draw(view, proj, shown.eye, target, candidates, [&](const water_reflection_view& mirror) {
                scene_haze mirrored = backdrop.prepare(&course.game.place.backdrop, mirror.eye, course.game.area.extent, light.haze_tint);
                mirrored.reflection_level = mirror.level;
                lighting.upload(light, mirrored, {}, nullptr);
                if (striped) {
                    paint_stripes(mirror.crop);
                    return;
                }
                backdrop.draw(mirror.view, mirror.sky_projection, 3, {}, true);
                const glm::mat4 view_proj = mirror.projection * mirror.view;
                tree.draw(view_proj, make_view_frustum(view_proj), 3, glm::vec2(0), &profile);
            }, &profile);
            lighting.upload(light, haze, {}, nullptr);
            water.draw_surfaces(&ponds, candidates, proj * view, target, style, weather, reflections.surfaces(), 3, timers, &profile);
            pictures[whole ? 0 : 1] = read_colors(scene, width, height);
            pixels[whole ? 0 : 1] = profile.work.reflection_pixels;
        }
        int different = 0;
        float peak = 0.0f;
        std::vector<std::array<std::uint8_t, 3>> rgb;
        for (std::size_t i = 0; i < pictures[0].size(); ++i) {
            const glm::vec3 delta = glm::abs(glm::vec3(pictures[1][i] - pictures[0][i]));
            const float largest = std::max({delta.x, delta.y, delta.z});
            different += largest > visible_delta ? 1 : 0;
            peak = std::max(peak, largest);
            std::array<std::uint8_t, 3> pixel{};
            for (int c = 0; c < 3; ++c) { pixel[static_cast<std::size_t>(c)] = static_cast<std::uint8_t>(255.0f * std::clamp(delta[c] * 8.0f, 0.0f, 1.0f)); }
            rgb.push_back(pixel);
        }
        const bool agrees = static_cast<float>(different) <= allowed_share * static_cast<float>(pictures[0].size()) && peak <= allowed_peak;
        cropped_total += pixels[1];
        whole_total += pixels[0];
        std::printf("  water crop %-15s %-7s %5llu of %5llu mirror pixels, %d pixels differ (peak %.3f)%s\n", shown.name,
                    striped ? "stripes" : "scene", static_cast<unsigned long long>(pixels[1]), static_cast<unsigned long long>(pixels[0]), different, peak,
                    agrees ? "" : ": FAILED");
        if (!agrees) {
            write_check_bmp(out / (std::string("water_crop_diff_") + (striped ? "stripes_" : "") + shown.name + ".bmp"), width, height, rgb);
            ok = false;
        }
    }
    }
    const bool smaller = cropped_total < whole_total;
    if (!smaller) { std::printf("  water crop: no case captured fewer pixels than the whole picture FAILED\n"); }
    reflections.shutdown();
    tree.shutdown();
    return ok && smaller;
}
