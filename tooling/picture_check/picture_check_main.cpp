// Rendered checks of the picture through the game's own shaders, on shipped
// courses (tooling/picture_check/README.md). Exits 1 when any fails.
#include "check_support.h"
#include "grass_check.h"
#include "grass_motion_check.h"
#include "grass_tile_check.h"
#include "water_check.h"
#include "tree_check.h"
#include "tree_image_check.h"
#include "tree_season_check.h"
#include "shadow_receiver_check.h"
#include "profile_check.h"
#include "turf_review_check.h"
#include "crowd_review_check.h"
#include "eyepiece_check.h"
#include "preset_check.h"
#include "terrain_lod_check.h"

#include "core/window.h"
#include "game/game_content.h"

#include <cstdio>
#include <string>
#include <vector>

constexpr int shipped_lines = 360;
constexpr int check_window_width = 320;
constexpr int check_window_height = 240;

int main(const int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string root = GOLFPP_ASSETS_DIR;
    // --grass or --water runs only that check; the rest name courses.
    std::vector<std::string> ids;
    bool profile_wanted = false;
    bool isolate_components = false;
    bool night = false;
    bool motion_wanted = false;
    bool tiles_wanted = false;
    bool eyepiece_wanted = false;
    bool presets_wanted = false;
    bool receivers_wanted = false;
    std::optional<std::string> turf_label;
    std::optional<std::string> crowd_label;
    std::string crowd_against;
    bool diagnose_wanted = false;
    bool tree_images_wanted = false;
    bool trees_wanted = false;
    std::vector<int> motion_lines{360, 480, 720};
    std::optional<float> motion_min_pixels;
    bool grass_wanted = true;
    bool water_wanted = true;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--profile") { profile_wanted = true; }
        else if (argument == "--isolate") { isolate_components = true; }
        else if (argument == "--night") { night = true; }
        else if (argument == "--motion") { motion_wanted = true; }
        else if (argument.rfind("--turf=", 0) == 0) { turf_label = argument.substr(7); }
        else if (argument.rfind("--crowd=", 0) == 0) { crowd_label = argument.substr(8); }
        else if (argument.rfind("--against=", 0) == 0) { crowd_against = argument.substr(10); }
        else if (argument == "--diagnose") { diagnose_wanted = true; }
        else if (argument == "--tree-images") { tree_images_wanted = true; }
        else if (argument == "--trees") { trees_wanted = true; }
        else if (argument == "--tiles") { tiles_wanted = true; }
        else if (argument == "--eyepiece") { eyepiece_wanted = true; }
        else if (argument == "--presets") { presets_wanted = true; }
        else if (argument == "--shadow-receivers") { receivers_wanted = true; grass_wanted = false; water_wanted = false; }
        else if (argument.rfind("--min-pixels=", 0) == 0) { motion_min_pixels = std::stof(argument.substr(13)); }
        else if (argument.rfind("--lines=", 0) == 0) { motion_lines = {std::stoi(argument.substr(8))}; }
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
    if (!display.init("golf++ picture check", profile_wanted ? 1920 : check_window_width, profile_wanted ? 1080 : check_window_height, false, root + "/interface/icons/golfpp-icon.bmp", true)) {
        return 1;
    }
    if (tree_images_wanted) {
        const bool images_ok = check_tree_images(*palette, root, tree_image_scope::full);
        display.shutdown();
        return images_ok ? 0 : 1;
    }
    if (trees_wanted) {
        const bool models_ok = check_trees(*palette, root);
        const bool seasons_ok = check_tree_seasons(*palette, content.content->tuning.trees, root);
        const bool trees_ok = check_tree_images(*palette, root, tree_image_scope::gate) && models_ok && seasons_ok;
        display.shutdown();
        return trees_ok ? 0 : 1;
    }
    bool passed = profile_wanted || receivers_wanted ||
        (check_trees(*palette,root) && check_tree_seasons(*palette,content.content->tuning.trees,root) && check_tree_images(*palette,root,tree_image_scope::gate));
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
        if (turf_label) {
            const std::filesystem::path review = std::filesystem::path(GOLFPP_BUILD_DIR) / "turf_review";
            passed = run_turf_review(loaded, *content.content, *palette, root, display.sdl_window(), {*turf_label, review, diagnose_wanted}) && passed;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            continue;
        }
        if (crowd_label) {
            const std::filesystem::path review = std::filesystem::path(GOLFPP_BUILD_DIR) / "crowd_review";
            passed = run_crowd_review(loaded, *content.content, *palette, root, display.sdl_window(), {*crowd_label, crowd_against, review}) && passed;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            continue;
        }
        if (eyepiece_wanted) {
            const eyepiece_check_result eyepiece = check_eyepiece(loaded, *content.content, *palette, root, display.sdl_window(), out / id);
            std::printf("  eyepiece: %d pictures, %d differ: %s\n", eyepiece.pictures, eyepiece.failures,
                        eyepiece.ran && eyepiece.failures == 0 ? "ok" : "FAILED");
            passed = eyepiece.ran && eyepiece.failures == 0 && passed;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            continue;
        }
        if (presets_wanted) {
            const preset_check_result presets = check_presets(loaded, *content.content, *palette, root, display.sdl_window());
            std::printf("  presets: %d pictures, %d wrong: %s\n", presets.pictures, presets.failures,
                        presets.ran && presets.failures == 0 ? "ok" : "FAILED");
            passed = presets.ran && presets.failures == 0 && passed;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            continue;
        }
        if (tiles_wanted) {
            const grass_tile_check_result tiles = check_grass_tiles(loaded, *content.content, *palette, root, display.sdl_window(), out / id, true);
            std::printf("  grass tiles: %s\n", tiles.ran && tiles.failures == 0 ? "ok" : "FAILED");
            passed = tiles.ran && tiles.failures == 0 && passed;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            continue;
        }
        if (motion_wanted) {
            const grass_motion_result motion = run_grass_motion(loaded, *content.content, *palette, root, display.sdl_window(),
                                                                motion_lines, out / id, motion_min_pixels);
            passed = grass_motion_passes(motion) && passed;
            continue;
        }
        if (grass_wanted) {
            const grass_check_result grass = check_grass(loaded, *palette, root, out / id);
            const bool grass_ok = grass.ran && grass.wrong_pixels == 0 && grass.changed_roots == 0 && grass.checked_roots > 0 && grass.blade_pixels > 0;
            std::printf("  grass: %d pictures, %lld blade pixels, %lld wrong: %s\n", grass.pictures, grass.blade_pixels,
                        grass.wrong_pixels, grass_ok ? "ok" : "FAILED");
            std::printf("  walking: %lld retained roots, %lld changed\n", grass.checked_roots, grass.changed_roots);
            passed = passed && grass_ok;
            // The rough as a walker sees it: its steadiness at the shipped line count.
            const grass_motion_result motion = run_grass_motion(loaded, *content.content, *palette, root, display.sdl_window(),
                                                                {shipped_lines}, out / id);
            const bool motion_ok = grass_motion_passes(motion);
            std::printf("  grass motion: %zu sequences: %s\n", motion.measures.size(), motion_ok ? "ok" : "FAILED");
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            passed = passed && motion_ok;
            const grass_tile_check_result tiles = check_grass_tiles(loaded, *content.content, *palette, root, display.sdl_window(), out / id, false);
            std::printf("  grass tiles: %s\n", tiles.ran && tiles.failures == 0 ? "ok" : "FAILED");
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            passed = passed && tiles.ran && tiles.failures == 0;
        }
        if (grass_wanted || receivers_wanted) {
            const shadow_receiver_result receivers = check_shadow_receivers(loaded, *content.content, *palette, root, display.sdl_window(), out / id);
            const bool receivers_ok = receivers.ran && receivers.unset == 0 && receivers.programs >= 6;
            std::printf("  shadow receivers: %d programs, %d unset, %d pictures: %s\n", receivers.programs, receivers.unset, receivers.pictures, receivers_ok ? "ok" : "FAILED");
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            passed = passed && receivers_ok;
            if (receivers_wanted) { continue; }
        }
        if (grass_wanted) {
            const eyepiece_check_result eyepiece = check_eyepiece(loaded, *content.content, *palette, root, display.sdl_window(), out / id);
            const bool eyepiece_ok = eyepiece.ran && eyepiece.failures == 0;
            std::printf("  eyepiece: %d pictures, %d differ: %s\n", eyepiece.pictures, eyepiece.failures, eyepiece_ok ? "ok" : "FAILED");
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
            passed = passed && eyepiece_ok;
        }
        if (water_wanted) {
            const water_check_result water = check_water(loaded, *palette, root, out / id);
            const bool water_ok = water.ran && water.failures == 0;
            std::printf("  water: %d pictures, %d failed: %s\n", water.pictures, water.failures, water_ok ? "ok" : "FAILED");
            passed = passed && water_ok;
            const terrain_lod_check_result levels = check_terrain_lod(loaded, *content.content, *palette, root, display.sdl_window(), out / id);
            passed = passed && levels.failures == 0;
            SDL_SetWindowSize(display.sdl_window(), check_window_width, check_window_height);
        }
    }
    display.shutdown();
    std::printf("%s\n", passed ? "all passed" : "FAILED (pictures in build/<preset>/picture_check/<course>)");
    return passed ? 0 : 1;
}
