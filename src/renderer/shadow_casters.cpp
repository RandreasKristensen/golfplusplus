#include "renderer/shadow_casters.h"

#include "renderer/fence_batch.h"
#include "renderer/hole_sign_batch.h"

#include <vector>

std::vector<glm::vec3> static_caster_corners(const render_hole_signs* signs, const render_fences* fences) {
    std::vector<glm::vec3> corners;
    const auto add = [&corners](const auto& vertices) {
        const std::size_t whole = vertices.size() - vertices.size() % 3;
        for (std::size_t i = 0; i < whole; ++i) {
            corners.push_back(vertices[i].position);
        }
    };
    if (signs != nullptr) { add(signs->vertices); }
    if (fences != nullptr) { add(fences->posts); }
    return corners;
}
