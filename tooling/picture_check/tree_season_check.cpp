#include "tree_season_check.h"
#include "game/tree_season.h"
#include "renderer/falling_leaf.h"
#include "renderer/gl_loader.h"
#include "renderer/lighting_uniforms.h"
#include "renderer/render_tree.h"
#include "renderer/tree_model.h"
#include "renderer/tree_renderer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>

namespace {
constexpr int side = 512;
// The grass's motion bound (grass_motion_check.cpp's far_change_limit): a moving
// picture changes at most this much more than one of twice the lines does.
constexpr double change_limit = 1.3;

std::size_t look_index(const lighting_palette& palette, const char* name) {
    for (std::size_t i = 0; i < palette.trees.size(); ++i) {
        if (palette.trees[i].name == name) { return i; }
    }
    return 0;
}

std::vector<float> depths(const framebuffer& target, const int size) {
    target.bind();
    std::vector<float> out(static_cast<std::size_t>(size) * static_cast<std::size_t>(size));
    glReadPixels(0, 0, size, size, GL_DEPTH_COMPONENT, GL_FLOAT, out.data());
    return out;
}

// Every leaf in front of what else is drawn shows at its centre, and every
// pixel the leaves changed lies on a leaf.
bool check_falling_leaves(const lighting_palette& palette, const std::string& root) {
    tree_renderer trees;
    framebuffer target;
    lighting_uniforms lighting;
    if (!trees.init(check_shader_path(root, "tree.vert"), check_shader_path(root, "tree.frag"), palette.trees, palette.tree_impostor) ||
        !target.init(side, side, {framebuffer_color::signed_hdr, framebuffer_depth::drawn, false, false}) || !lighting.init()) {
        return false;
    }
    const falling_leaf_style& style = palette.falling_leaves;
    trees.set_falling_leaves(style);
    const std::size_t look = look_index(palette, "beech");
    tree_body tree;
    tree.shape = {0.4f, 5, 4, 9};
    tree.base = {0.0f, 1.0f, 0.0f};
    trees.set_trees({tree}, 1, nullptr);
    const glm::vec3 eye(0.0f, 12.0f, -9.0f);
    const auto vp = glm::perspective(glm::radians(36.0f), 1.0f, 0.1f, 200.0f) * glm::lookAt(eye, glm::vec3(0, 2, 0), glm::vec3(0, 1, 0));
    const float focal = 0.5f * static_cast<float>(side) / std::tan(glm::radians(18.0f));
    render_lighting light;
    light.ambient_sky = glm::vec3(1);
    light.ambient_ground = glm::vec3(1);
    scene_haze haze;
    haze.eye = eye;
    haze.full_distance = 100000;
    lighting.upload(light, haze, {}, nullptr);
    const glm::vec2 wind(0.3f, 0.2f);
    const float daily = 0.6f;  // far more than any real day, so every slot is busy
    const auto draw = [&](const float falling, const float seconds) {
        render_tree_species season{palette.trees[look].name, 0.5f, 1.0f, 0.0f};
        season.falling = falling;
        trees.set_seasons({season});
        target.bind();
        glViewport(0, 0, side, side);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        trees.draw(vp, make_view_frustum(vp), seconds, wind, nullptr, tree_eye{eye, focal, 0.0f});
    };
    const render_tree_instance instance = tree_instance_for(tree);
    const tree_box crown = tree_crown_box(palette.trees[look]);
    const float per_cycle = falling_leaves_per_cycle(style, instance, daily, glm::length(wind));
    bool passed = per_cycle >= 1.0f;
    int shown = 0;
    int missed = 0;
    int stray = 0;
    for (const float seconds : {3.0f, 11.5f, 27.25f, 40.0f, 55.5f, 71.0f, 90.25f, 104.0f}) {
        draw(0.0f, seconds);
        const std::vector<glm::vec4> bare = read_colors(target, side, side);
        const std::vector<float> bare_depth = depths(target, side);
        draw(daily, seconds);
        const std::vector<glm::vec4> leafy = read_colors(target, side, side);
        std::vector<glm::vec3> centres;  // x, y in pixels, z the leaf's half size in pixels
        for (int index = 0; index < style.most_per_tree; ++index) {
            const falling_leaf leaf = place_falling_leaf(style, crown, instance, index, per_cycle, seconds, wind);
            if (!leaf.shown) { continue; }
            const glm::vec4 clip = vp * glm::vec4(leaf.centre, 1.0f);
            if (clip.w <= 0.0f) { continue; }
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const glm::vec2 pixel = (glm::vec2(ndc) * 0.5f + 0.5f) * static_cast<float>(side);
            const float half = focal * glm::length(leaf.across) / clip.w;
            centres.emplace_back(pixel, half);
            // Only a leaf covering a few pixels as it is turned surely covers its centre's.
            const auto on_screen = [&](const glm::vec3& point) {
                const glm::vec4 c = vp * glm::vec4(point, 1.0f);
                return (glm::vec2(c) / c.w * 0.5f + 0.5f) * static_cast<float>(side);
            };
            const glm::vec2 a = on_screen(leaf.centre + leaf.across) - on_screen(leaf.centre - leaf.across);
            const glm::vec2 b = on_screen(leaf.centre + leaf.along) - on_screen(leaf.centre - leaf.along);
            const float area = std::abs(a.x * b.y - a.y * b.x);
            const int x = static_cast<int>(pixel.x);
            const int y = static_cast<int>(pixel.y);
            if (x < 1 || y < 1 || x >= side - 1 || y >= side - 1 || area < 12.0f) { continue; }
            // Hidden behind the crown: nothing to see.
            if (ndc.z * 0.5f + 0.5f > bare_depth[static_cast<std::size_t>(y * side + x)] - 1.0e-4f) { continue; }
            ++shown;
            bool found = false;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const std::size_t i = static_cast<std::size_t>((y + dy) * side + x + dx);
                    found = found || glm::any(glm::greaterThan(glm::abs(leafy[i] - bare[i]), glm::vec4(1.0e-4f)));
                }
            }
            missed += found ? 0 : 1;
        }
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                const std::size_t i = static_cast<std::size_t>(y * side + x);
                if (!glm::any(glm::greaterThan(glm::abs(leafy[i] - bare[i]), glm::vec4(1.0e-4f)))) { continue; }
                const glm::vec2 at(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f);
                const bool on_leaf = std::any_of(centres.begin(), centres.end(), [&](const glm::vec3& c) {
                    return glm::length(at - glm::vec2(c)) <= c.z * 1.5f + 2.0f;
                });
                stray += on_leaf ? 0 : 1;
            }
        }
    }
    passed = passed && shown >= 10 && missed == 0 && stray == 0;
    std::printf("trees: falling leaves replay falling_leaf.h, %d in sight, %d missing, %d pixels off any leaf: %s\n", shown, missed,
                stray, passed ? "ok" : "FAILED");
    trees.shutdown();
    target.shutdown();
    lighting.shutdown();
    return passed;
}

// Mean frame-to-frame change of a walking eye's pictures of bare crowns, drawn
// at `size` and averaged down to `side`.
double walking_change(tree_renderer& trees, framebuffer& target, lighting_uniforms& lighting, const int size, const float distance) {
    std::vector<float> previous;
    double change = 0.0;
    int frames = 0;
    const int scale = size / side;
    for (int step = 0; step < 24; ++step) {
        const glm::vec3 eye(-0.6f + 0.05f * static_cast<float>(step), 5.0f, -distance);
        const auto vp = glm::perspective(glm::radians(24.0f), 1.0f, 0.1f, 400.0f) * glm::lookAt(eye, eye + glm::vec3(0, 0.03f, distance), glm::vec3(0, 1, 0));
        render_lighting light;
        light.ambient_sky = glm::vec3(1);
        light.ambient_ground = glm::vec3(1);
        scene_haze haze;
        haze.eye = eye;
        haze.full_distance = 100000;
        lighting.upload(light, haze, {}, nullptr);
        target.bind();
        glViewport(0, 0, size, size);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        trees.draw(vp, make_view_frustum(vp), 0.0f, glm::vec2(0.0f), nullptr);
        const std::vector<glm::vec4> picture = read_colors(target, size, size);
        std::vector<float> luminance(static_cast<std::size_t>(side) * side, 0.0f);
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const glm::vec4& c = picture[static_cast<std::size_t>(y * size + x)];
                luminance[static_cast<std::size_t>((y / scale) * side + x / scale)] +=
                    (0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b) / static_cast<float>(scale * scale);
            }
        }
        if (!previous.empty()) {
            double sum = 0.0;
            for (std::size_t i = 0; i < luminance.size(); ++i) { sum += std::abs(luminance[i] - previous[i]); }
            change += sum / static_cast<double>(luminance.size());
            ++frames;
        }
        previous = std::move(luminance);
    }
    return change / std::max(frames, 1);
}

bool check_bare_crowns(const lighting_palette& palette, const std::string& root) {
    tree_renderer trees;
    framebuffer target;
    lighting_uniforms lighting;
    if (!trees.init(check_shader_path(root, "tree.vert"), check_shader_path(root, "tree.frag"), palette.trees, palette.tree_impostor) ||
        !target.init(2 * side, 2 * side, {framebuffer_color::signed_hdr, framebuffer_depth::drawn, false, false}) || !lighting.init()) {
        return false;
    }
    bool passed = true;
    for (const char* name : {"beech", "oak", "birch", "willow"}) {
        const std::size_t look = look_index(palette, name);
        render_tree_species winter{palette.trees[look].name, 0.0f, 1.0f, 0.0f};
        trees.set_seasons({winter});
        std::vector<tree_body> row;
        for (int i = 0; i < 3; ++i) {
            tree_body one;
            one.shape = {0.4f, 5, 4, 9};
            one.shape.species = 0;
            one.base = {static_cast<float>(i - 1) * 9.0f, 0.0f, 0.0f};
            row.push_back(one);
        }
        trees.set_trees(row, look + 100, nullptr);
        for (const float distance : {30.0f, 70.0f}) {
            const double scene = walking_change(trees, target, lighting, side, distance);
            const double reference = walking_change(trees, target, lighting, 2 * side, distance);
            const double ratio = scene / std::max(reference, 1.0e-9);
            const bool ok = ratio <= change_limit;
            std::printf("trees: a bare %s walked past at %.0f m changes %.2fx as much as twice the lines: %s\n", name, distance, ratio,
                        ok ? "ok" : "FAILED");
            passed = passed && ok;
        }
    }
    trees.shutdown();
    target.shutdown();
    lighting.shutdown();
    return passed;
}

// Each species in summer and in winter, near (its model) and far (its image),
// lit by a low sun against a grey sky, for a person to look at.
bool write_season_pictures(const lighting_palette& palette, const tree_tuning& tuning, const std::string& root) {
    tree_renderer trees;
    framebuffer target;
    lighting_uniforms lighting;
    constexpr int width = 768;
    constexpr int height = 384;
    if (!trees.init(check_shader_path(root, "tree.vert"), check_shader_path(root, "tree.frag"), palette.trees, palette.tree_impostor) ||
        !target.init(width, height, {framebuffer_color::signed_hdr, framebuffer_depth::drawn, false, false}) || !lighting.init()) {
        return false;
    }
    const std::filesystem::path out = std::filesystem::path(GOLFPP_BUILD_DIR) / "picture_check" / "trees";
    std::error_code error;
    std::filesystem::create_directories(out, error);
    bool written = !error;
    for (const tree_look& look : palette.trees) {
        const std::optional<std::uint8_t> kind = species_index(tuning, look.name);
        if (!kind) { continue; }
        const tree_species& species = tuning.species[*kind];
        tree_body tree;
        tree.shape = look.shape == crown_shape::dome ? tree_shape{0.0f, 0.0f, 2.0f, 2.5f, tree_kind::bush} : tree_shape{0.4f, 5, 4, 9};
        trees.set_trees({tree}, std::hash<std::string>{}(look.name), nullptr);
        const float tall = tree.shape.trunk_height + tree.shape.leaf_height;
        for (const bool winter : {false, true}) {
            const float day = static_cast<float>(winter ? day_of_year(1, 15) : midsummer_day_of_year);
            const render_tree_species season{look.name, foliage_share(species, day), turned_share(species, day),
                                             fresh_share(species, day), withered_share(tuning, species, day)};
            trees.set_seasons({season});
            for (const bool distant : {false, true}) {
                // Far: the eye backs off until the image is drawn, the picture zoomed to the same framing.
                const float distance = (distant ? 12.0f : 1.6f) * tall;
                const float fov = 2.0f * std::atan(0.75f * tall / distance);
                const glm::vec3 eye(0.0f, 0.5f * tall, -distance);
                const auto vp = glm::perspective(fov, 2.0f, 0.1f, 2000.0f) *
                                glm::lookAt(eye, glm::vec3(0.0f, 0.5f * tall, 0.0f), glm::vec3(0, 1, 0));
                render_lighting light;
                light.sun_direction = glm::normalize(glm::vec3(-0.6f, 0.45f, -0.65f));
                light.sun_color = glm::vec3(1.6f, 1.45f, 1.2f);
                light.ambient_sky = glm::vec3(0.45f, 0.5f, 0.6f);
                light.ambient_ground = glm::vec3(0.25f, 0.23f, 0.2f);
                scene_haze haze;
                haze.eye = eye;
                haze.full_distance = 100000;
                lighting.upload(light, haze, {}, nullptr);
                target.bind();
                glViewport(0, 0, width, height);
                glClearColor(0.62f, 0.66f, 0.7f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                const float focal = 0.5f * static_cast<float>(height) / std::tan(0.5f * fov);
                trees.draw(vp, make_view_frustum(vp), 0.0f, glm::vec2(0.0f), nullptr, tree_eye{eye, distant ? focal : 0.0f, 0.0f});
                const std::vector<glm::vec4> picture = read_colors(target, width, height);
                std::vector<std::array<std::uint8_t, 3>> rgb;
                rgb.reserve(picture.size());
                for (const glm::vec4& c : picture) {
                    const glm::vec3 shown = glm::clamp(glm::vec3(c), 0.0f, 1.0f) * 255.0f;
                    rgb.push_back({static_cast<std::uint8_t>(shown.r), static_cast<std::uint8_t>(shown.g), static_cast<std::uint8_t>(shown.b)});
                }
                const std::string name = look.name + (winter ? "_winter" : "_summer") + (distant ? "_far" : "_near") + ".bmp";
                written = write_check_bmp(out / name, width, height, rgb) && written;
            }
        }
    }
    std::printf("trees: summer and winter pictures of every species in %s: %s\n", out.string().c_str(), written ? "ok" : "FAILED");
    trees.shutdown();
    target.shutdown();
    lighting.shutdown();
    return written;
}
}

bool check_tree_seasons(const lighting_palette& palette, const tree_tuning& tuning, const std::string& root) {
    const bool leaves = check_falling_leaves(palette, root);
    const bool crowns = check_bare_crowns(palette, root);
    const bool pictures = write_season_pictures(palette, tuning, root);
    return leaves && crowns && pictures;
}
