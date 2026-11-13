#include "renderer/shadow_casters.h"

#include "physics/vector_math.h"
#include "renderer/bag_batch.h"
#include "renderer/fence_batch.h"
#include "renderer/hole_sign_batch.h"
#include "renderer/remote_avatar_batch.h"

#include <algorithm>
#include <vector>

namespace {
// The triangles of `vertices` with a corner within `reach` of `eye`.
template <typename Vertex>
void append_near_triangles(world_marker_batch& figures, const std::vector<Vertex>& vertices, const glm::vec3& eye,
                           const float reach) {
    std::vector<glm::vec3> corners;
    for (std::size_t i = 0; i + 2 < vertices.size(); i += 3) {
        const bool near = std::any_of(vertices.begin() + static_cast<std::ptrdiff_t>(i),
                                      vertices.begin() + static_cast<std::ptrdiff_t>(i + 3),
                                      [&](const Vertex& vertex) { return horizontal_distance(vertex.position, eye) <= reach; });
        if (near) {
            corners.insert(corners.end(), {vertices[i].position, vertices[i + 1].position, vertices[i + 2].position});
        }
    }
    if (!corners.empty()) {
        figures.append_world_triangles(corners, glm::vec3(0.0f), 0.0f, marker_stencil::none);
    }
}

void append_flagsticks(world_marker_batch& figures, const render_data& data, const scene_colors& colors,
                       const float texel_meters) {
    const flag_pose flag{data.weather.flag_wind, data.weather.flag_lift, data.weather.flag_phase, data.weather.flag_flap};
    const pole_view pole{data.flagstick_radius_meters, data.lighting.sun_direction, texel_meters};
    const auto standing = [&](const glm::vec3& pin) {
        if (std::find(data.flags_out.begin(), data.flags_out.end(), pin) == data.flags_out.end()) {
            append_pin_flagstick(figures, pin, data.pin_visual_height_meters, colors.markers, colors.glow, flag, pole);
        }
    };
    if (data.show_hole) {
        standing(data.pin_position);
    }
    for (const glm::vec3& pin : data.pin_markers) {
        standing(pin);
    }
    for (const auto& [foot, tip] : data.lying_flags) {
        append_lying_flagstick(figures, foot, tip, colors.markers, colors.glow);
    }
}
}

void build_shadow_casters(world_marker_batch& figures, prop_batch& models, const render_data& data, const scene_colors& colors,
                          const prop_catalog& props, const shadow_style& map, const prop_batch* static_models) {
    figures.clear();
    models.clear();
    const float texel_meters = map.map_resolution > 0 ? map.map_extent / static_cast<float>(map.map_resolution) : 0.0f;
    append_flagsticks(figures, data, colors, texel_meters);
    if (data.hole_signs != nullptr) {
        append_near_triangles(figures, data.hole_signs->vertices, data.camera_position, map.map_extent);
    }
    if (data.fences != nullptr) {
        append_near_triangles(figures, data.fences->posts, data.camera_position, map.map_extent);
    }
    append_carts_and_bags(models, data, props, colors);
    if (static_models) { models.append(*static_models); }
    if (data.my_figure) {
        append_remote_avatar(figures, *data.my_figure, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
        append_figure_gear(models, *data.my_figure, data.avatar_eye_height, props, colors);
    }
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        append_remote_avatar(figures, avatar, data.avatar_eye_height, data.ball_visual_radius_meters, colors);
        append_figure_gear(models, avatar, data.avatar_eye_height, props, colors);
    }
}
