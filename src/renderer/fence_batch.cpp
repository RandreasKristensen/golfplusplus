#include "renderer/fence_batch.h"

#include "physics/vector_math.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace {
// Posts are drawn at least this thick, so the low-res target keeps them as a
// pixel instead of losing them between scanlines.
constexpr float min_drawn_post_half_width = 0.06f;

void append_quad(std::vector<fence_vertex>& out, const fence_vertex& a, const fence_vertex& b, const fence_vertex& c,
                 const fence_vertex& d) {
    out.insert(out.end(), {a, b, c, a, c, d});
}

// A square post from `foot` up `height`, one face per side.
void append_post(std::vector<fence_vertex>& out, const glm::vec3& foot, const float half_width, const float height,
                 const glm::vec3& post_color) {
    const glm::vec3 corners[4] = {foot + glm::vec3(-half_width, 0.0f, -half_width), foot + glm::vec3(half_width, 0.0f, -half_width),
                                  foot + glm::vec3(half_width, 0.0f, half_width), foot + glm::vec3(-half_width, 0.0f, half_width)};
    const glm::vec3 up(0.0f, height, 0.0f);
    for (int i = 0; i < 4; ++i) {
        const glm::vec3& a = corners[i];
        const glm::vec3& b = corners[(i + 1) % 4];
        const glm::vec3 normal = safe_normalize(horizontal((a + b) * 0.5f - foot), glm::vec3(1.0f, 0.0f, 0.0f));
        const auto vertex = [&](const glm::vec3& p) { return fence_vertex{p, normal, post_color, glm::vec2(0.0f)}; };
        append_quad(out, vertex(a), vertex(b), vertex(b + up), vertex(a + up));
    }
}

// The net from pole `a` to pole `b` (their feet), `height` tall. The texture
// runs on along the fence from `along_start`, so its mesh continues across
// the poles; it is drawn from the texture, not a colour.
void append_net(std::vector<fence_vertex>& out, const glm::vec3& a, const glm::vec3& b, const float height,
                const float along_start) {
    const glm::vec3 untinted(1.0f);
    const glm::vec3 across = horizontal(b - a);
    const float length = glm::length(across);
    const glm::vec3 normal = safe_normalize(glm::vec3(-across.z, 0.0f, across.x), glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::vec3 up(0.0f, height, 0.0f);
    const float u0 = along_start / net_tile_metres;
    const float u1 = (along_start + length) / net_tile_metres;
    const float v1 = height / net_tile_metres;
    append_quad(out, fence_vertex{a, normal, untinted, glm::vec2(u0, 0.0f)},
                fence_vertex{b, normal, untinted, glm::vec2(u1, 0.0f)},
                fence_vertex{b + up, normal, untinted, glm::vec2(u1, v1)},
                fence_vertex{a + up, normal, untinted, glm::vec2(u0, v1)});
}
}

render_fences build_render_fences(const play_area& area, const std::uint64_t revision, const glm::vec3& post_color,
                                  const float pole_radius) {
    render_fences fences;
    fences.revision = revision;
    const float half_width = std::max(min_drawn_post_half_width, pole_radius);
    for (const course_world_fence& fence : area.fences) {
        float along = 0.0f;
        for (std::size_t i = 0; i < fence.poles.size(); ++i) {
            append_post(fences.posts, fence.poles[i], half_width, fence.height, post_color);
            if (i + 1 < fence.poles.size()) {
                append_net(fences.nets, fence.poles[i], fence.poles[i + 1], fence.height, along);
                along += glm::length(horizontal(fence.poles[i + 1] - fence.poles[i]));
            }
        }
    }
    return fences;
}
