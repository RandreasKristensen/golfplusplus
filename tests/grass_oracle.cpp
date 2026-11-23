#include "grass_oracle.h"

#include <algorithm>
#include <cmath>
#include <glm/common.hpp>
#include <glm/geometric.hpp>

#include "renderer/grass_field.h"

namespace {
float cross(const glm::vec2& a, const glm::vec2& b) { return a.x * b.y - a.y * b.x; }

float edge_distance_squared(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) {
    const glm::vec2 ab = b - a;
    const float t = std::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1.0e-12f), 0.0f, 1.0f);
    const glm::vec2 delta = p - a - t * ab;
    return glm::dot(delta, delta);
}

void triangle(dense_grass& field, const render_terrain_vertex& a, const render_terrain_vertex& b,
              const render_terrain_vertex& c, const bool heights, const bool kind_only = false) {
    const bool bare = a.blades() < 0.5f && b.blades() < 0.5f && c.blades() < 0.5f;
    if (!heights && !bare) { return; }
    const glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z), pc(c.position.x, c.position.z);
    const float area = cross(pb - pa, pc - pa);
    if (std::abs(area) < 1.0e-10f) { return; }
    const glm::vec2 low = (glm::min(pa, glm::min(pb, pc)) - field.origin) / grass_field_spacing;
    const glm::vec2 high = (glm::max(pa, glm::max(pb, pc)) - field.origin) / grass_field_spacing;
    const int x0 = std::max(0, static_cast<int>(std::ceil(low.x)));
    const int z0 = std::max(0, static_cast<int>(std::ceil(low.y)));
    const int x1 = std::min(field.columns - 1, static_cast<int>(std::floor(high.x)));
    const int z1 = std::min(field.rows - 1, static_cast<int>(std::floor(high.y)));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const glm::vec2 p = field.origin + glm::vec2(x, z) * grass_field_spacing;
            const float wb = cross(p - pa, pc - pa) / area;
            const float wc = cross(pb - pa, p - pa) / area;
            const float wa = 1.0f - wb - wc;
            if (std::min(wa, std::min(wb, wc)) < -1.0e-5f) { continue; }
            glm::vec3& sample = field.samples[static_cast<std::size_t>(z) * field.columns + x];
            if (heights) {
                if (!kind_only) { sample.x = wa * a.position.y + wb * b.position.y + wc * c.position.y; }
                sample.z = std::max(1.0f, wa * a.blades() + wb * b.blades() + wc * c.blades());
            } else {
                sample.y = 0.0f;
            }
        }
    }
}

struct edge {
    glm::vec2 a, b;
};

void bare_edges(const render_static_mesh& mesh, std::vector<edge>& edges) {
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const auto& a = mesh.vertices[mesh.indices[i]];
        const auto& b = mesh.vertices[mesh.indices[i + 1]];
        const auto& c = mesh.vertices[mesh.indices[i + 2]];
        if (!(a.blades() < 0.5f && b.blades() < 0.5f && c.blades() < 0.5f)) { continue; }
        const glm::vec2 pa(a.position.x, a.position.z), pb(b.position.x, b.position.z), pc(c.position.x, c.position.z);
        edges.insert(edges.end(), {edge{pa, pb}, edge{pb, pc}, edge{pc, pa}});
    }
}
}

dense_grass bake_dense_grass(const render_static_mesh& ground, const render_static_mesh& overlay, const std::vector<tee_box>& tees) {
    dense_grass field;
    if (!ground.bounds.valid || ground.indices.empty()) { return field; }
    field.origin = glm::floor(glm::vec2(ground.bounds.min.x, ground.bounds.min.z) / grass_field_spacing) * grass_field_spacing;
    const glm::vec2 end(ground.bounds.max.x, ground.bounds.max.z);
    const glm::ivec2 size = glm::ivec2(glm::ceil((end - field.origin) / grass_field_spacing)) + 1;
    field.columns = size.x;
    field.rows = size.y;
    const float cap = grass_field_distance_cap * grass_field_distance_cap;
    field.samples.assign(static_cast<std::size_t>(size.x) * size.y, glm::vec3(-1000.0f, cap, 0.0f));
    const auto each = [](const render_static_mesh& mesh, auto&& visit) {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            visit(mesh.vertices[mesh.indices[i]], mesh.vertices[mesh.indices[i + 1]], mesh.vertices[mesh.indices[i + 2]]);
        }
    };
    each(ground, [&](const auto& a, const auto& b, const auto& c) { triangle(field, a, b, c, true); });
    each(overlay, [&](const auto& a, const auto& b, const auto& c) {
        if (a.blades() >= 1.5f && b.blades() >= 1.5f && c.blades() >= 1.5f) { triangle(field, a, b, c, true, true); }
    });
    std::vector<edge> edges;
    for (const render_static_mesh* mesh : {&ground, &overlay}) {
        each(*mesh, [&](const auto& a, const auto& b, const auto& c) { triangle(field, a, b, c, false); });
        bare_edges(*mesh, edges);
    }
    for (const tee_box& tee : tees) {
        const glm::vec3 along = tee.down_hole * tee.half_length;
        const glm::vec3 across(-tee.down_hole.z * tee.half_width, 0.0f, tee.down_hole.x * tee.half_width);
        render_terrain_vertex corners[4];
        corners[0].position = tee.center - along - across;
        corners[1].position = tee.center + along - across;
        corners[2].position = tee.center + along + across;
        corners[3].position = tee.center - along + across;
        for (int i = 0; i < 4; ++i) {
            const glm::vec3& a = corners[i].position;
            const glm::vec3& b = corners[(i + 1) % 4].position;
            edges.push_back({{a.x, a.z}, {b.x, b.z}});
        }
        triangle(field, corners[0], corners[1], corners[2], false);
        triangle(field, corners[0], corners[2], corners[3], false);
    }
    for (int z = 0; z < field.rows; ++z) {
        for (int x = 0; x < field.columns; ++x) {
            glm::vec3& sample = field.samples[static_cast<std::size_t>(z) * field.columns + x];
            const float border = grass_field_spacing * static_cast<float>(std::min({x, z, field.columns - 1 - x, field.rows - 1 - z}));
            if (sample.z == 0.0f) {
                sample.y = 0.0f;
            } else if (sample.y > 0.0f) {
                const glm::vec2 point = field.origin + glm::vec2(x, z) * grass_field_spacing;
                float nearest = cap;
                for (const edge& e : edges) { nearest = std::min(nearest, edge_distance_squared(point, e.a, e.b)); }
                sample.y = std::sqrt(std::min(nearest, border * border));
            }
        }
    }
    for (int z = 0; z + 1 < field.rows; ++z) {
        for (int x = 0; x + 1 < field.columns; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * field.columns + x;
            const std::size_t cell[4] = {i, i + 1, i + field.columns, i + field.columns + 1};
            if (field.samples[cell[0]].z == 0.0f || field.samples[cell[1]].z == 0.0f || field.samples[cell[2]].z == 0.0f ||
                field.samples[cell[3]].z == 0.0f) {
                for (const std::size_t node : cell) { field.samples[node].y = 0.0f; }
            }
        }
    }
    return field;
}

glm::vec3 sample_dense_grass(const dense_grass& field, const glm::vec2& root) {
    if (field.columns < 2 || field.rows < 2) { return glm::vec3(0.0f); }
    const glm::vec2 at = (root - field.origin) / grass_field_spacing;
    if (at.x < 0 || at.y < 0 || at.x >= field.columns - 1 || at.y >= field.rows - 1) { return glm::vec3(0.0f); }
    const glm::ivec2 low = glm::ivec2(glm::floor(at));
    const glm::vec2 f = at - glm::vec2(low);
    return glm::mix(glm::mix(field.node(low.x, low.y), field.node(low.x + 1, low.y), f.x),
                    glm::mix(field.node(low.x, low.y + 1), field.node(low.x + 1, low.y + 1), f.x), f.y);
}
