#pragma once

// The game's whole renderer on a checked course at a chosen window size and
// scene line count, so a check can capture the scene before picture
// processing and the final TV output of the same frame. Only the stuff a
// caller asks to leave out (trees, ponds, props) is removed; nothing shipped
// changes.

#include "check_support.h"
#include "core/light_frame.h"
#include "core/sky_frame.h"
#include "core/weather_frame.h"
#include "renderer/fence_batch.h"
#include "renderer/floodlight_batch.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/water_batch.h"
#include "game/golf_cart.h"
#include "game/picture_loader.h"
#include "game/prop_loader.h"
#include "game/text_assets.h"
#include "renderer/renderer.h"
#include "renderer/render_data.h"
#include "renderer/remote_avatar_batch.h"

#include <SDL.h>

#include <optional>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

// One captured frame: the scene's pixels (linear, bottom row first) and the
// window's.
struct rig_capture {
    int scene_width = 0;
    int scene_height = 0;
    std::vector<glm::vec4> scene;
    int final_width = 0;
    int final_height = 0;
    std::vector<glm::vec4> final_picture;
    std::vector<glm::vec4> eyepiece;  // the rangefinder's picture, scene-sized; empty without one
};

struct rig_camera {
    glm::vec3 eye{0.0f};
    glm::vec3 target{0.0f, 0.0f, 1.0f};
    float fov_degrees = 60.0f;
    float clock_seconds = 0.0f;
    glm::vec2 wind{0.0f};  // m/s, x and z
    bool ponds = false;    // draw the course's water, mirrors included
    bool trees = false;    // draw the course's trees and their shadows
    float magnification = 0.0f;  // above 0: held up the rangefinder at this zoom
    std::optional<glm::vec3> ball;  // draw a ball with its centre here
    // Other players, their balls and the carts they drive (core/render_gear.h scripted_carts).
    std::vector<render_remote_avatar> figures;
    std::vector<render_remote_ball> balls;
    std::vector<cart_in_world> carts;
};

class picture_rig {
public:
    // The window is resized to `window_height` (16:9). `target_lines` replaces the
    // picture's shipped line count, and `blade_min_pixels` the grass's least blade
    // width, for this rig only.
    bool init(SDL_Window* window, const check_course& course, const game_content& content,
              const lighting_palette& palette, const std::string& asset_root, int window_height, int target_lines,
              std::optional<float> blade_min_pixels = std::nullopt);
    void shutdown();
    // The hour on the course's clock (a winter date when `winter`, a true night
    // under floodlights at 24) and the weather shown, for the frames to come.
    void set_conditions(float hour, bool winter, const shown_weather& weather);
    const render_lighting& lighting() const { return light_; }
    // Leaves props, and trees and ponds unless the camera asks, out.
    rig_capture capture(const rig_camera& camera);
    void mask_eyepiece(bool on) { draw_.mask_eyepiece(on); }
    const renderer& drawn_by() const { return draw_; }
    bool limit_grass_tiles(int layers) { return draw_.limit_grass_tiles(layers); }
    const render_water& water() const { return water_; }

private:
    SDL_Window* window_ = nullptr;
    const check_course* course_ = nullptr;
    const game_content* content_ = nullptr;
    lighting_palette palette_;
    prop_catalog_load_result props_;
    std::optional<text_assets> text_;
    std::optional<picture_style> picture_;
    render_water water_;
    render_fences fences_;
    render_floodlights lamps_;
    render_hole_signs signs_;
    renderer draw_;
    render_lighting light_;
    tree_shadow_cache tree_blobs_;
    blob_ground_memo blob_ground_;
    weather_presentation weather_;
    int window_width_ = 0;
    int window_height_ = 0;
    bool ready_ = false;
};
