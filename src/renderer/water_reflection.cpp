#include "renderer/water_reflection.h"
#include "renderer/frustum.h"
#include "renderer/render_data.h"
#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

glm::mat4 water_mirror(const float level) {
    glm::mat4 mirror(1.0f);
    mirror[1][1] = -1.0f;
    mirror[3][1] = 2.0f * level;
    return mirror;
}

water_reflection_view reflect_water_view(const glm::mat4& view, const glm::mat4& projection,
                                          const glm::vec3& eye, const float level) {
    water_reflection_view reflected;
    const glm::mat4 mirror = water_mirror(level);
    reflected.level = level;
    reflected.view = view * mirror;
    reflected.eye = glm::vec3(mirror * glm::vec4(eye, 1.0f));
    reflected.projection = projection;
    // Replace the near clipping plane with y >= level. In clip space the
    // near plane is row2 + row3; its opposite far corner stays on z = w.
    const glm::vec4 plane = glm::transpose(glm::inverse(reflected.view)) * glm::vec4(0, 1, 0, -level);
    const auto sign = [](const float x) { return x < 0.0f ? -1.0f : 1.0f; };
    const glm::vec4 far = glm::inverse(projection) * glm::vec4(sign(plane.x), sign(plane.y), 1, 1);
    const float denominator = glm::dot(plane, far);
    const glm::vec4 near = plane * (2.0f / std::max(denominator,1.0e-6f));
    for (int column = 0; column < 4; ++column) { reflected.projection[column][2] = near[column] - projection[column][3]; }
    return reflected;
}

std::vector<float> visible_water_levels(const render_water& water, const glm::mat4& view_proj) {
    const view_frustum frustum = make_view_frustum(view_proj);
    std::vector<float> levels;
    for (std::size_t i = 0; i + 2 < water.triangles.size(); i += 3) {
        const glm::vec3 a = water.triangles[i].position, b = water.triangles[i + 1].position, c = water.triangles[i + 2].position;
        if (std::find(levels.begin(), levels.end(), a.y) == levels.end() &&
            frustum_intersects_aabb(frustum, glm::min(a, glm::min(b, c)), glm::max(a, glm::max(b, c)))) {
            levels.push_back(a.y);
        }
    }
    std::sort(levels.begin(), levels.end());
    return levels;
}

render_data make_reflected_scene(const render_data& scene,const glm::vec3& eye) {
    render_data reflected=scene;
    reflected.camera_position=eye;
    reflected.address_feet.reset();
    reflected.dap_hand.reset();
    if(scene.my_figure) {
        auto figure=*scene.my_figure;
        figure.smoking=scene.smoking;
        figure.drinking=scene.drinking;
        reflected.remote_avatars.push_back(figure);
        reflected.my_club.reset();
    }
    return reflected;
}
