#pragma once

// Planar mirrors, GL-free geometry: one real scene projection per visible
// water level, clipped to the part of the world above that plane.
#include <vector>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include "renderer/water_batch.h"

struct water_reflection_view {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 eye{0.0f};
    float level = 0.0f;
};

glm::mat4 water_mirror(float level);
water_reflection_view reflect_water_view(const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& eye, float level);
std::vector<float> visible_water_levels(const render_water& water, const glm::mat4& view_proj);

struct water_reflection {
    float level = 0.0f;
    unsigned int texture = 0;
    glm::mat4 view_proj{1.0f};
};

struct render_data;
// The same world seen by the mirror, including my physical figure and its
// gear through the ordinary figure path, without the camera's viewmodel.
render_data make_reflected_scene(const render_data& scene,const glm::vec3& eye);
