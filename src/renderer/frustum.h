#pragma once

#include <array>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// GL-free view frustum culling helpers (unit tested without an OpenGL context).
//
// Convention: planes are extracted from an OpenGL-style clip matrix
// `proj * view` (GLM default, clip z in [-w, w]) using the Gribb/Hartmann
// method. GLM matrices are column-major, so row i of the matrix is
// (m[0][i], m[1][i], m[2][i], m[3][i]). A point p is inside plane k when
// dot(normal, p) + distance >= 0. Planes are normalized, so that value is a
// signed distance in world units.

struct frustum_plane {
    glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
    float distance = 0.0f;
};

enum class frustum_plane_id : int {
    left = 0,
    right,
    bottom,
    top,
    near_plane,
    far_plane,
    count
};

struct view_frustum {
    std::array<frustum_plane, static_cast<int>(frustum_plane_id::count)> planes{};
};

// Extracts the six normalized planes of an OpenGL clip matrix (proj * view).
view_frustum make_view_frustum(const glm::mat4& view_proj);

// Conservative AABB test (positive-vertex test): returns false only when the
// box lies entirely outside at least one plane by more than `margin` world
// units. Boxes straddling a plane, or containing the camera, are visible.
// Like every plane-only test it may keep some boxes that sit outside a frustum
// corner, which is harmless (they are clipped by the GPU).
bool frustum_intersects_aabb(const view_frustum& frustum,
                             const glm::vec3& box_min,
                             const glm::vec3& box_max,
                             float margin = 0.05f);
