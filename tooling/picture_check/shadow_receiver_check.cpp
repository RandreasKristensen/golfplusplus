#include "shadow_receiver_check.h"

#include "picture_rig.h"
#include "renderer/gl_loader.h"
#include "renderer/renderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <glm/trigonometric.hpp>

namespace {
constexpr int window_height = 360;
constexpr float eye_height = 2.0f;
constexpr float eye_distance = 14.0f;
constexpr float cluster_radius = 14.0f;

struct condition {
    const char* name;
    float hour;
    bool winter;
};

// The tree with the most others within `cluster_radius`: shade falls on trees and grass there.
const tree_body* densest_tree(const std::vector<tree_body>& trees) {
    const tree_body* best = nullptr;
    int most = -1;
    for (const tree_body& tree : trees) {
        int neighbours = 0;
        for (const tree_body& other : trees) {
            neighbours += glm::distance(glm::vec2(tree.base.x, tree.base.z), glm::vec2(other.base.x, other.base.z)) < cluster_radius ? 1 : 0;
        }
        if (neighbours > most) {
            most = neighbours;
            best = &tree;
        }
    }
    return best;
}

bool write_scene(const std::filesystem::path& path, const rig_capture& shot) {
    std::vector<std::array<std::uint8_t, 3>> rgb(shot.scene.size());
    for (std::size_t i = 0; i < rgb.size(); ++i) {
        for (int c = 0; c < 3; ++c) {
            rgb[i][static_cast<std::size_t>(c)] =
                static_cast<std::uint8_t>(std::lround(255.0f * std::pow(std::clamp(shot.scene[i][c], 0.0f, 1.0f), 1.0f / 2.2f)));
        }
    }
    return write_check_bmp(path, shot.scene_width, shot.scene_height, rgb);
}

// Counts every linked program with a u_shadow_map and those not reading the shadow unit.
void scan_programs(shadow_receiver_result& result) {
    for (unsigned int program = 1; program < 2048; ++program) {
        if (glIsProgram(program) == GL_FALSE) { continue; }
        const GLint location = glGetUniformLocation(program, "u_shadow_map");
        if (location < 0) { continue; }
        GLint unit = -1;
        glGetUniformiv(program, location, &unit);
        ++result.programs;
        if (unit != renderer::shadow_map_unit) {
            ++result.unset;
            std::printf("  shadow receivers FAILED: program %u reads unit %d, not %d\n", program, unit, renderer::shadow_map_unit);
        }
    }
}
}  // namespace

shadow_receiver_result check_shadow_receivers(const check_course& course, const game_content& content,
                                              const lighting_palette& palette, const std::string& asset_root,
                                              SDL_Window* window, const std::filesystem::path& out) {
    shadow_receiver_result result;
    const tree_body* tree = densest_tree(course.game.static_anchors.trees);
    picture_rig rig;
    if (tree == nullptr || !rig.init(window, course, content, palette, asset_root, window_height, window_height)) { return result; }
    const glm::vec3 aim = tree->base + glm::vec3(0.0f, 2.0f, 0.0f);
    const char* sides[] = {"south", "east", "north", "west"};
    const condition conditions[] = {{"noon", 12.0f, false}, {"low_sun", 12.0f, true}, {"evening", 18.0f, false}};
    for (const condition& when : conditions) {
        rig.set_conditions(when.hour, when.winter, shown_weather{});
        for (int side = 0; side < 4; ++side) {
            const float angle = glm::radians(90.0f * static_cast<float>(side));
            rig_camera camera;
            camera.target = aim;
            camera.eye = tree->base + glm::vec3(std::sin(angle) * eye_distance, eye_height, -std::cos(angle) * eye_distance);
            camera.fov_degrees = 55.0f;
            camera.trees = true;
            camera.clock_seconds = 5.0f;
            rig.capture(camera);
            const rig_capture shot = rig.capture(camera);
            result.pictures += write_scene(out / (std::string("shadow_") + when.name + "_" + sides[side] + ".bmp"), shot) ? 1 : 0;
        }
    }
    scan_programs(result);
    rig.shutdown();
    result.ran = true;
    return result;
}
