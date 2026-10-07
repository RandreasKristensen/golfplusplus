#pragma once

// The course's backdrop drawn behind the scene, first, so everything covers
// it: the sky panorama, then the land panorama (far ground, hills and
// treelines) over it, the land fading into the course's haze colour with
// distance as drawn into its alpha (see tooling/art/make_art.py). Sky, land
// and haze are separate so the sky and haze colour can change on their own
// (time of day) without redrawing the land.

#include "game/course_definition.h"
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
              unsigned int screen_vao);
    void shutdown();
    // Draws `backdrop` (images relative to the asset root, loaded when they
    // differ from the last ones; null or a missing image draws nothing,
    // leaving the clear colour) seen from `eye`, and returns the haze the
    // scene should fade into, which reaches the far ground's haze by the
    // edge of the play area (`area_extent` from its centre). Leaves depth
    // testing on.
    scene_haze draw(const course_backdrop* backdrop, const glm::vec3& eye, float area_extent, const glm::mat4& view,
                    const glm::mat4& proj);
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
    unsigned int screen_vao_ = 0;
};
