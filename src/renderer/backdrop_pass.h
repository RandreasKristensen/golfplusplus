#pragma once

// The one home of the sky: the course's backdrop, drawn after the opaque
// scene at the far plane so it shades only what nothing covers. The sky is
// the course's sky panorama (its clouds) giving way by the time of day to a
// gradient with a glow towards the sun and clouds lit by it, then the sun,
// the moon and the stars (sky_style); the land panorama (far ground, hills
// and treelines) goes over it, fading into the course's haze colour with
// distance as drawn into its alpha (see tooling/art/make_art.py). Sky, land and haze are separate so
// each follows the time of day from the frame_lighting block (lighting.glsl)
// without redrawing the pictures.

#include "game/course_definition.h"
#include "game/lighting_loader.h"
#include "renderer/scene_haze.h"
#include "renderer/shader.h"
#include "renderer/texture.h"

#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

class backdrop_pass {
public:
    // `screen_vao` is a full-screen quad (position.xyz first) owned by the caller.
    bool init(const std::string& asset_root, const std::string& vertex_path, const std::string& fragment_path,
              unsigned int screen_vao, const sky_style& sky);
    void shutdown();
    // Makes `backdrop` (images relative to the asset root) the one drawn,
    // loading its images when they differ from the last ones, and returns
    // the haze the scene fades into seen from `eye` (make_scene_haze, in the
    // time of day's `haze_tint`). Null or a missing image draws nothing and
    // has no haze.
    scene_haze prepare(const course_backdrop* backdrop, const glm::vec3& eye, float area_extent, const glm::vec3& haze_tint);
    // Draws the prepared backdrop in this frame's light (the frame_lighting
    // block must hold the prepared haze) wherever the depth buffer is still
    // clear; `seconds` runs the stars' twinkle. Leaves depth testing on.
    void draw(const glm::mat4& view, const glm::mat4& proj, float seconds);
    void set_profile(frame_profile* profile) { shader_.set_profile(profile); }

private:
    void load(const course_backdrop& backdrop);

    std::string asset_root_;
    shader_program shader_;
    texture sky_;
    texture land_;
    // What sky_ and land_ were loaded from (or failed to).
    std::string sky_image_;
    std::string land_image_;
    // Haze of the land panorama's bottom row (the far ground), 0..1.
    float far_ground_haze_ = 0.0f;
    float sky_brightness_ = 0.0f;  // the sky panorama's mean
    float haze_amount_ = 0.0f;     // the prepared backdrop's
    unsigned int screen_vao_ = 0;
};
