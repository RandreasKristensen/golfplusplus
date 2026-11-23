#pragma once

// The rough as a walker sees it, frame after frame. A fixed rough patch is
// captured through the game's renderer while the eye stands, turns, walks
// toward it and across it, with the world clock frozen or running and the
// wind off or on. Each scene frame (before picture processing) is compared
// with a reference of the same frame drawn at twice the lines and box-filtered,
// which separates the world's own change (geometry, coverage) from aliasing
// of its blades; the final TV output of the same frames shows what remains
// after tape grain. A check_course view that reproduces a defect must fail
// here; grass_tile_check.h reruns it with a grass tile cache too small for one
// view, so every frame is drawn in batches.

#include "check_support.h"

#include <SDL.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// A straight run over the ground the eye walks and looks along.
struct motion_run {
    glm::vec2 start{0.0f};
    glm::vec2 heading{0.0f, 1.0f};
    float length = 0.0f;
    bool ponds = false;  // the course's water is drawn, mirrors included
};

// One sequence of camera poses over the rough patch.
enum class motion_kind { still, still_clock, turn, walk_toward, walk_across };

struct motion_measure {
    int target_lines = 0;
    motion_kind kind = motion_kind::still;
    bool wind = false;
    // Root mean square of scene minus reference luma over the patch, relative
    // to the patch's mean luma: aliasing of blades finer than a pixel.
    double alias = 0.0;
    // Mean frame-to-frame luma change over the patch in the scene, in its
    // reference (the world's own change) and in the final TV picture.
    double scene_change = 0.0;
    double reference_change = 0.0;
    double final_change = 0.0;
    double mean_luma = 0.0;
    // Per distance band (2.5-8, 8-22, 22-44, 44-88 m): alias, as above.
    double band_alias[4] = {0.0, 0.0, 0.0, 0.0};
    // Per band: the scene's mean luma over the reference's, less one.
    double band_bias[4] = {0.0, 0.0, 0.0, 0.0};
    // Per band: scene change over reference change.
    double band_ratio[4] = {0.0, 0.0, 0.0, 0.0};
    int frames = 0;
};

struct grass_motion_result {
    bool ran = false;
    std::vector<motion_measure> measures;
};

// Measures every sequence at each of `scene_lines` for the course's longest
// rough run. The scene is drawn by a hidden window of that many lines times
// its enlargement. `blade_min_pixels` replaces the grass's shipped least blade
// width for the pictures shown (not the reference), to reproduce a defect.
// `tile_layers` above zero caps the shown renderer's grass tile cache. Pictures of each sequence's last frame go to `out`.
grass_motion_result run_grass_motion(const check_course& course, const game_content& content, const lighting_palette& palette,
                                     const std::string& asset_root, SDL_Window* window, const std::vector<int>& scene_lines,
                                     const std::filesystem::path& out,
                                     std::optional<float> blade_min_pixels = std::nullopt, int tile_layers = 0);

// The same measures over any `run` (fairway, green, a pond's bank).
grass_motion_result run_motion_along(const motion_run& run, const check_course& course, const game_content& content,
                                     const lighting_palette& palette, const std::string& asset_root, SDL_Window* window,
                                     const std::vector<int>& scene_lines, const std::filesystem::path& out,
                                     std::optional<float> blade_min_pixels = std::nullopt, int tile_layers = 0);

// True when the shipped-line-count measures show no more aliasing and
// excess change than the thresholds in grass_motion_check.cpp allow.
bool grass_motion_passes(const grass_motion_result& result);
