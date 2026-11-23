#include "picture_rig.h"

#include "core/render_frame.h"
#include "game/model_kinds.h"
#include "game/sky_clock.h"
#include "renderer/gl_loader.h"

#include <algorithm>

bool picture_rig::init(SDL_Window* window, const check_course& course, const game_content& content,
                       const lighting_palette& palette, const std::string& asset_root, const int window_height,
                       const int target_lines, const std::optional<float> blade_min_pixels) {
    shutdown();
    window_ = window;
    course_ = &course;
    content_ = &content;
    palette_ = palette;
    if (blade_min_pixels) { palette_.grass.blade_min_pixels = *blade_min_pixels; }
    window_height_ = window_height;
    window_width_ = window_height * 16 / 9;
    SDL_SetWindowSize(window, window_width_, window_height_);
    int drawable_width = 0;
    int drawable_height = 0;
    SDL_GL_GetDrawableSize(window, &drawable_width, &drawable_height);
    if (drawable_width != window_width_ || drawable_height != window_height_) {
        return false;
    }
    props_ = load_props(asset_root);
    text_ = load_text_assets(asset_root);
    picture_ = load_picture_style(asset_root);
    if (!props_.props || !text_ || !picture_) { return false; }
    picture_->scene_lines = {target_lines};
    if (!draw_.init(window, asset_root, *picture_, palette_, *props_.props)) { return false; }
    const game_state& game = course.game;
    water_ = build_render_water(game.area, game.terrain_render_revision);
    fences_ = build_render_fences(game.area, game.terrain_render_revision, props_.props->at(fence_model).color("post"),
                                  game.tuning.fence.pole_radius);
    lamps_ = build_render_floodlights(game.area, game.terrain_render_revision, props_.props->at(floodlight_model));
    signs_ = build_render_hole_signs(game.area, hole_sign_texts(game), {}, *text_, palette_.colors,
                                     props_.props->at(hole_sign_model), game.terrain_render_revision);
    refresh_tree_shadows(tree_blobs_, game, palette_.shadows);
    weather_.started = true;
    set_conditions(12.0f, false, shown_target(game.weather));
    ready_ = true;
    return true;
}

void picture_rig::set_conditions(const float hour, const bool winter, const shown_weather& weather) {
    sky_controls sky;
    sky.pinned_hour = hour;
    if (winter) { sky.pinned_date = days_from_civil(1989, 12, 21); }
    light_ = make_render_lighting(course_->game, palette_, sky, 0, weather);
    weather_.shown = weather;
}

void picture_rig::shutdown() {
    if (ready_) { draw_.shutdown(); }
    ready_ = false;
}

rig_capture picture_rig::capture(const rig_camera& camera) {
    rig_capture result;
    if (!ready_) { return result; }
    const game_state& game = course_->game;
    const render_meshes meshes{&course_->terrain, &course_->overlay, &signs_, &fences_, &water_, &lamps_, &course_->grass};
    render_data data = make_render_data(game, *text_, content_->skills, meshes, *props_.props, camera.fov_degrees, true, nullptr, camera.carts);
    data.camera_position = camera.eye;
    data.camera_target = camera.target;
    data.camera_fov_degrees = camera.fov_degrees;
    data.clock_seconds = camera.clock_seconds;
    data.lighting = light_;
    data.remote_avatars.insert(data.remote_avatars.end(), camera.figures.begin(), camera.figures.end());
    data.remote_balls.insert(data.remote_balls.end(), camera.balls.begin(), camera.balls.end());
    if (camera.ball) {
        data.show_ball = true;
        data.ball_position = *camera.ball;
    }
    add_frame_lights(data, game, palette_, tree_blobs_, blob_ground_);
    data.weather = make_frame_weather(weather_, game, palette_, data);
    data.weather.surface_wind = glm::vec3(camera.wind.x, 0.0f, camera.wind.y);
    if (!camera.trees) {
        data.trees = nullptr;
        data.tree_shadows = nullptr;
    }
    if (!camera.ponds) { data.water = nullptr; }
    data.show_rangefinder = camera.magnification > 0.0f;
    data.rangefinder_eyepiece = render_eyepiece{camera.magnification, game.tuning.rangefinder.surround_brightness,
                                                game.tuning.rangefinder.surround_saturation};
    data.world_props = nullptr;
    data.floodlights = nullptr;
    data.fences = nullptr;
    draw_.render(data, *text_);
    const framebuffer& scene = draw_.scene_target();
    result.scene_width = scene.width();
    result.scene_height = scene.height();
    result.scene = read_colors(scene, scene.width(), scene.height());
    if (data.show_rangefinder) {
        const framebuffer& eyepiece = draw_.eyepiece_target();
        result.eyepiece = read_colors(eyepiece, eyepiece.width(), eyepiece.height());
    }
    framebuffer::bind_default();
    result.final_width = window_width_;
    result.final_height = window_height_;
    result.final_picture.resize(static_cast<std::size_t>(window_width_) * static_cast<std::size_t>(window_height_));
    glReadPixels(0, 0, window_width_, window_height_, GL_RGBA, GL_FLOAT, result.final_picture.data());
    SDL_GL_SwapWindow(window_);
    return result;
}
