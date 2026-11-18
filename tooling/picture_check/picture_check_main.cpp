// Rendered checks of the picture through the game's own shaders, on shipped
// courses (tooling/picture_check/README.md). Exits 1 when any fails.
#include "check_support.h"
#include "grass_check.h"
#include "water_check.h"
#include "tree_check.h"
#include "profile_check.h"

#include "core/window.h"
#include "game/game_content.h"

#include <cstdio>
#include <string>
#include <vector>

int main(const int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root = GOLFPP_ASSETS_DIR;
    // --grass or --water runs only that check; the rest name courses.
    std::vector<std::string> ids;
    bool profile_wanted = false;
    bool isolate_components = false;
    bool night = false;
    bool grass_wanted = true;
    bool water_wanted = true;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--profile") { profile_wanted = true; }
        else if (argument == "--isolate") { isolate_components = true; }
        else if (argument == "--night") { night = true; }
        else if (argument == "--grass" || argument == "--water") {
            grass_wanted = argument == "--grass";
            water_wanted = argument == "--water";
        } else {
            ids.push_back(argument);
        }
    }
    const std::filesystem::path out = std::filesystem::path(GOLFPP_BUILD_DIR) / "picture_check";
    std::error_code made;
    std::filesystem::create_directories(out, made);
    const game_content_load_result content = load_game_content(root);
    const std::optional<lighting_palette> palette = load_lighting(root);
    if (!content.content || !palette) {
        std::fprintf(stderr, "cannot load the game's content: %s\n", content.error.c_str());
        return 1;
    }
    if (ids.empty()) {
        for (const course_definition& course : content.content->courses) {
            ids.push_back(course.id);
        }
    }
    window display;
    if (!display.init("golf++ picture check", profile_wanted ? 1920:320, profile_wanted ? 1080:240, false, root + "/interface/icons/golfpp-icon.bmp", true)) {
        return 1;
    }
    bool passed = profile_wanted || check_trees(*palette,root);
    for (const std::string& id : ids) {
        const course_definition* course = nullptr;
        for (const course_definition& candidate : content.content->courses) {
            course = candidate.id == id ? &candidate : course;
        }
        if (course == nullptr) {
            std::fprintf(stderr, "no course %s\n", id.c_str());
            passed = false;
            continue;
        }
        std::printf("%s\n", id.c_str());
        const check_course loaded = load_check_course(*content.content, *course, *palette);
        std::filesystem::create_directories(out / id, made);
        if (profile_wanted) {
            passed=profile_picture(loaded,*content.content,*palette,root,out/id,display.sdl_window(),isolate_components,night) && passed;
            continue;
        }
        if (grass_wanted) {
            const grass_check_result grass = check_grass(loaded, *palette, root, out / id);
            const bool grass_ok = grass.ran && grass.wrong_pixels == 0 && grass.changed_roots == 0 && grass.checked_roots > 0 && grass.blade_pixels > 0;
            std::printf("  grass: %d pictures, %lld blade pixels, %lld wrong: %s\n", grass.pictures, grass.blade_pixels,
                        grass.wrong_pixels, grass_ok ? "ok" : "FAILED");
            std::printf("  walking: %lld retained roots, %lld changed\n", grass.checked_roots, grass.changed_roots);
            passed = passed && grass_ok;
        }
        if (water_wanted) {
            const water_check_result water = check_water(loaded, *palette, root, out / id);
            const bool water_ok = water.ran && water.failures == 0;
            std::printf("  water: %d pictures, %d failed: %s\n", water.pictures, water.failures, water_ok ? "ok" : "FAILED");
            passed = passed && water_ok;
        }
    }
    display.shutdown();
    std::printf("%s\n", passed ? "all passed" : "FAILED (pictures in build/<preset>/picture_check/<course>)");
    return passed ? 0 : 1;
}
