#include "renderer/marker_scene.h"

#include <algorithm>
#include <cmath>

#include <glm/trigonometric.hpp>

world_marker_scene marker_scene_for(const render_data& data, const int scene_height_pixels) {
    world_marker_scene markers;
    markers.cups_apart = true;
    markers.show_hole = data.show_hole;
    markers.pin_position = data.pin_position;
    markers.pin_markers = &data.pin_markers;
    markers.pin_marker_heights = &data.pin_marker_heights;
    markers.flags_out = &data.flags_out;
    markers.lying_flags = &data.lying_flags;
    markers.collectibles = &data.collectibles;
    markers.cup_radius_meters = data.cup_radius_meters;
    markers.cup_depth_meters = data.cup_depth_meters;
    markers.pin_visual_height_meters = data.pin_visual_height_meters;
    markers.flagstick_radius_meters = data.flagstick_radius_meters;
    markers.camera_position = data.camera_position;
    markers.pixel_per_meter = 2.0f * std::tan(glm::radians(std::max(1.0f, data.camera_fov_degrees)) * 0.5f) /
        static_cast<float>(std::max(1, scene_height_pixels));
    // The eyepiece's zoom is the projection scaled by the magnification (zoomed_projection).
    markers.zoom_pixel_per_meter = data.show_rangefinder
        ? markers.pixel_per_meter / std::max(1.0f, data.rangefinder_eyepiece.magnification)
        : 0.0f;
    markers.flag =flag_pose{data.weather.flag_wind, data.weather.flag_lift, data.weather.flag_phase, data.weather.flag_flap};
    markers.address_feet = data.address_feet ? &*data.address_feet : nullptr;
    markers.my_figure = data.my_figure ? &*data.my_figure : nullptr;
    markers.ball_visual_radius_meters = data.ball_visual_radius_meters;
    markers.remote_avatars = &data.remote_avatars;
    markers.remote_balls = &data.remote_balls;
    markers.avatar_eye_height = data.avatar_eye_height;
    return markers;
}
