#include "preset_check.h"

#include "game/picture_loader.h"
#include "picture_rig.h"
#include "renderer/picture_size.h"

#include <algorithm>
#include <cstdio>
#include <set>

namespace {
constexpr int window_heights[] = {720, 1080, 1440};
constexpr float noon_hour = 12.0f;

bool varies(const std::vector<glm::vec4>& pixels) {
    std::set<int> reds;
    for (const glm::vec4& pixel : pixels) {
        reds.insert(static_cast<int>(pixel.r * 255.0f));
    }
    return reds.size() > 8;
}
}

preset_check_result check_presets(const check_course& course, const game_content& content, const lighting_palette& palette,
                                  const std::string& asset_root, SDL_Window* window) {
    preset_check_result result;
    const std::optional<picture_style> picture = load_picture_style(asset_root);
    if (!picture) { return result; }
    result.ran = true;
    rig_camera camera;
    camera.eye = course.game.player.position + glm::vec3(0.0f, 1.7f, 0.0f);
    camera.target = camera.eye + glm::vec3(0.0f, -0.1f, 1.0f);
    for (const int height : window_heights) {
        for (const int lines : picture->scene_lines) {
            picture_rig rig;
            if (!rig.init(window, course, content, palette, asset_root, height, lines)) {
                std::printf("  presets: cannot draw %d lines at %d rows\n", lines, height);
                ++result.failures;
                continue;
            }
            rig.set_conditions(noon_hour, false, shown_target(course.game.weather));
            const rig_capture capture = rig.capture(camera);
            const int width = height * 16 / 9;
            const picture_size expected = picture_size_for_window(width, height, lines, picture->ui_lines);
            const picture_size& drawn = rig.drawn_by().picture();
            const bool ok = drawn.factor == expected.factor && drawn.unit == expected.unit &&
                capture.scene_width == expected.width && capture.scene_height == expected.height &&
                capture.final_width == width && capture.final_height == height &&
                expected.width * expected.factor >= width && expected.height * expected.factor >= height &&
                varies(capture.scene) && varies(capture.final_picture);
            ++result.pictures;
            if (!ok) {
                ++result.failures;
                std::printf("  presets: %d lines at %dx%d: scene %dx%d, factor %d, unit %d, expected %dx%d, %d, %d\n", lines,
                            width, height, capture.scene_width, capture.scene_height, drawn.factor, drawn.unit, expected.width,
                            expected.height, expected.factor, expected.unit);
            }
            rig.shutdown();
        }
    }
    return result;
}
