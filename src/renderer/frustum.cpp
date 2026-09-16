#include "renderer/frustum.h"

#include <cmath>

#include <glm/geometric.hpp>
#include <glm/vec4.hpp>

namespace {

glm::vec4 matrix_row(const glm::mat4& m, const int row) {
    return glm::vec4(m[0][row], m[1][row], m[2][row], m[3][row]);
}

frustum_plane make_plane(const glm::vec4& coefficients) {
    frustum_plane plane;
    const glm::vec3 normal(coefficients.x, coefficients.y, coefficients.z);
    const float length = glm::length(normal);
    if (!(length > 1.0e-12f) || !std::isfinite(length)) {
        // Degenerate matrix: an always-inside plane keeps culling conservative.
        plane.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        plane.distance = 1.0e30f;
        return plane;
    }
    plane.normal = normal / length;
    plane.distance = coefficients.w / length;
    return plane;
}

} // namespace

view_frustum make_view_frustum(const glm::mat4& view_proj) {
    const glm::vec4 row0 = matrix_row(view_proj, 0);
    const glm::vec4 row1 = matrix_row(view_proj, 1);
    const glm::vec4 row2 = matrix_row(view_proj, 2);
    const glm::vec4 row3 = matrix_row(view_proj, 3);

    view_frustum frustum;
    frustum.planes[static_cast<int>(frustum_plane_id::left)] = make_plane(row3 + row0);
    frustum.planes[static_cast<int>(frustum_plane_id::right)] = make_plane(row3 - row0);
    frustum.planes[static_cast<int>(frustum_plane_id::bottom)] = make_plane(row3 + row1);
    frustum.planes[static_cast<int>(frustum_plane_id::top)] = make_plane(row3 - row1);
    // OpenGL clip space: -w <= z <= w.
    frustum.planes[static_cast<int>(frustum_plane_id::near_plane)] = make_plane(row3 + row2);
    frustum.planes[static_cast<int>(frustum_plane_id::far_plane)] = make_plane(row3 - row2);
    return frustum;
}

bool frustum_intersects_aabb(const view_frustum& frustum,
                             const glm::vec3& box_min,
                             const glm::vec3& box_max,
                             const float margin) {
    for (const frustum_plane& plane : frustum.planes) {
        const glm::vec3 positive_vertex(plane.normal.x >= 0.0f ? box_max.x : box_min.x,
                                        plane.normal.y >= 0.0f ? box_max.y : box_min.y,
                                        plane.normal.z >= 0.0f ? box_max.z : box_min.z);
        if (glm::dot(plane.normal, positive_vertex) + plane.distance < -margin) {
            return false;
        }
    }
    return true;
}
