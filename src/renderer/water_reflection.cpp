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
                                          const glm::vec3& eye, const float level, const glm::mat4& crop) {
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
    reflected.projection = crop * reflected.projection;
    reflected.sky_projection = crop * projection;
    return reflected;
}

std::vector<water_candidate> water_candidates(const render_water& water, const glm::mat4& view_proj, const glm::vec3& eye,
                                              const water_lookup_reach& reach) {
    const view_frustum frustum = make_view_frustum(view_proj);
    std::vector<water_candidate> candidates;
    for (std::size_t i = 0; i < water.levels.size(); ++i) {
        const water_level_range& range = water.levels[i];
        if (!frustum_intersects_aabb(frustum, range.min, range.max)) { continue; }
        bool in_view = false;
        bool covered = false;
        glm::vec2 low(1.0f);
        glm::vec2 high(0.0f);
        for (int c = 0; c < range.cluster_count; ++c) {
            const water_cluster& cluster = water.clusters[static_cast<std::size_t>(range.first_cluster + c)];
            if (!frustum_intersects_aabb(frustum, cluster.min, cluster.max)) { continue; }
            in_view = true;
            if (eye.y <= range.level) { continue; }
            const water_cluster_cover cover = water_cluster_coverage(cluster, range.level, view_proj, eye, reach);
            if (cover.kind == water_cover::outside) { continue; }
            covered = true;
            low = glm::min(low, glm::vec2(cover.rectangle.x, cover.rectangle.y));
            high = glm::max(high, glm::vec2(cover.rectangle.z, cover.rectangle.w));
        }
        // Seen from under it a surface reflects nothing and needs no rectangle.
        if (eye.y <= range.level) {
            if (in_view) { candidates.push_back({i, range.level, 0, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f)}); }
        } else if (covered) {
            candidates.push_back({i, range.level, 0, glm::vec4(low, high)});
        }
    }
    return candidates;
}

render_remote_avatar mirrored_figure(const render_data& scene) {
    render_remote_avatar figure=*scene.my_figure;
    figure.smoking=scene.smoking;
    figure.drinking=scene.drinking;
    return figure;
}

render_data make_reflected_scene(const render_data& scene,const glm::vec3& eye) {
    render_data reflected=scene;
    reflected.camera_position=eye;
    reflected.address_feet.reset();
    reflected.dap_hand.reset();
    if(scene.my_figure) {
        reflected.remote_avatars.push_back(mirrored_figure(scene));
        reflected.my_club.reset();
    }
    return reflected;
}
