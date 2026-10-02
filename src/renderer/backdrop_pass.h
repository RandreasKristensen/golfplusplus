#pragma once

// The course's backdrop: a panorama of sky and far-off land (a course's
// `backdrop` image) drawn behind the scene, first, so everything covers it.

#include "renderer/shader.h"
#include "renderer/texture.h"

#include <string>

#include <glm/mat4x4.hpp>

class backdrop_pass {
public:
    // `screen_vao` is a full-screen quad (position.xyz first) owned by the caller.
    bool init(const std::string& asset_root, const std::string& vertex_path, const std::string& fragment_path,
              unsigned int screen_vao);
    void shutdown();
    // Draws `image` (relative to the asset root; empty draws nothing, leaving
    // the clear colour), loading it when it differs from the last one. Leaves
    // depth testing on.
    void draw(const std::string& image, const glm::mat4& view, const glm::mat4& proj);
    void set_profile(frame_profile* profile) { shader_.set_profile(profile); }

private:
    std::string asset_root_;
    shader_program shader_;
    texture panorama_;
    std::string panorama_image_;  // what panorama_ was loaded from (or failed to)
    unsigned int screen_vao_ = 0;
};
