#pragma once

// The haze the drawn scene fades into with distance, so the far ground and
// trees melt into the course backdrop's land panorama instead of ending at a
// sharp edge. backdrop_pass works it out from the course's backdrop; the
// scene shaders that draw far-off things (terrain.frag, tree_instanced.vert)
// apply it as  amount = far_amount * min(distance / full_distance, 1)^2.

#include "renderer/shader.h"

#include <glm/vec3.hpp>

struct scene_haze {
    glm::vec3 color{0.0f};
    // Haze at full_distance and beyond: the far ground's haze in the land
    // panorama, so the edge of the drawn ground meets it without a seam.
    float far_amount = 0.0f;
    float full_distance = 1.0f;
    glm::vec3 eye{0.0f};
};

// Sets the u_haze_* uniforms on `shader`, which must be in use.
inline void set_scene_haze_uniforms(const shader_program& shader, const scene_haze& haze) {
    shader.set_vec3("u_haze_color", haze.color);
    shader.set_float("u_haze_far_amount", haze.far_amount);
    shader.set_float("u_haze_full_distance", haze.full_distance);
    shader.set_vec3("u_haze_eye", haze.eye);
}
