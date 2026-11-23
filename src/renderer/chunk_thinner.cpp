#include "renderer/chunk_thinner.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>

namespace {
float cross_xz(const glm::vec3& p, const glm::vec3& q, const glm::vec3& r) {
    return (q.x - p.x) * (r.z - p.z) - (q.z - p.z) * (r.x - p.x);
}

// The share of each of `a`, `b`, `c` at `at` (XZ), the same where they lie flat.
bool barycentric(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& at, float weights[3]) {
    const float area = cross_xz(a, b, c);
    if (area == 0.0f) {
        return false;
    }
    weights[0] = cross_xz(at, b, c) / area;
    weights[1] = cross_xz(a, at, c) / area;
    weights[2] = cross_xz(a, b, at) / area;
    return weights[0] >= -1.0e-4f && weights[1] >= -1.0e-4f && weights[2] >= -1.0e-4f;
}

// How far colour, normal, sky and horizon of `a` are from `b`'s.
float attribute_distance(const render_terrain_vertex& a, const render_terrain_vertex& b) {
    float most = std::max(glm::length(a.normal() - b.normal()), std::abs(a.rough() - b.rough()));
    most = std::max(most, std::abs(a.ao() - b.ao()));
    const glm::vec3 color = glm::abs(a.color() - b.color());
    most = std::max(most, std::max(color.x, std::max(color.y, color.z)));
    for (std::size_t way = 0; way < a.horizon.size(); ++way) {
        most = std::max(most, std::abs(static_cast<float>(a.horizon[way]) - static_cast<float>(b.horizon[way])) / 255.0f);
    }
    return most;
}

render_terrain_vertex blended(const render_terrain_vertex& a, const render_terrain_vertex& b, const render_terrain_vertex& c,
                              const float weights[3]) {
    render_terrain_vertex out;
    out.set_normal(a.normal() * weights[0] + b.normal() * weights[1] + c.normal() * weights[2]);
    out.set_color(a.color() * weights[0] + b.color() * weights[1] + c.color() * weights[2]);
    out.set_rough(a.rough() * weights[0] + b.rough() * weights[1] + c.rough() * weights[2]);
    out.set_ao(a.ao() * weights[0] + b.ao() * weights[1] + c.ao() * weights[2]);
    for (std::size_t way = 0; way < out.horizon.size(); ++way) {
        out.horizon[way] = static_cast<std::uint8_t>(std::lround(std::clamp(
            static_cast<float>(a.horizon[way]) * weights[0] + static_cast<float>(b.horizon[way]) * weights[1] +
                static_cast<float>(c.horizon[way]) * weights[2], 0.0f, 255.0f)));
    }
    return out;
}
}

bool chunk_thinner::load(const std::vector<std::uint32_t>& indices, const render_mesh_chunk& chunk, const std::vector<std::uint8_t>& shared,
    std::vector<std::uint32_t>& local_of) {
    global_.clear();
    triangles_.clear();
    for (std::uint32_t i = chunk.first_index; i + 2U < chunk.first_index + chunk.index_count; i += 3U) {
        triangle t;
        for (std::size_t corner = 0; corner < 3U; ++corner) {
            const std::uint32_t g = indices[i + corner];
            if (g >= vertices_.size()) {
                release(local_of);
                return false;
            }
            if (local_of[g] == none) {
                local_of[g] = static_cast<std::uint32_t>(global_.size());
                global_.push_back(g);
            }
            t.corner[corner] = local_of[g];
        }
        triangles_.push_back(t);
    }
    const std::size_t count = global_.size();
    incident_.assign(count, {});
    alive_.assign(count, 1);
    locked_.assign(count, 0);
    error_.assign(count, 0.0f);
    drift_.assign(count, 0.0f);
    for (std::uint32_t t = 0; t < triangles_.size(); ++t) {
        for (const std::uint32_t corner : triangles_[t].corner) {
            incident_[corner].push_back(t);
        }
    }
    for (std::uint32_t v = 0; v < count; ++v) {
        locked_[v] = shared[global_[v]] != 0 || !free_to_collapse(v);
    }
    release(local_of);
    collapses_ = 0;
    heap_ = {};
    for (std::uint32_t v = 0; v < count; ++v) {
        push(v);
    }
    return true;
}

bool chunk_thinner::thin_to(const float error_meters) {
    const std::size_t before = collapses_;
    while (!heap_.empty() && heap_.top().key <= error_meters) {
        const entry top = heap_.top();
        heap_.pop();
        if (!alive_[top.vertex]) {
            continue;
        }
        std::array<std::uint32_t, max_ring> ring{};
        std::size_t ring_count = 0;
        const collapse_choice choice = evaluate(top.vertex, ring, ring_count);
        if (!choice.valid) {
            continue;
        }
        if (choice.error > error_meters) {
            heap_.push({choice.error, top.vertex});
            continue;
        }
        collapse(top.vertex, choice, ring, ring_count);
    }
    return collapses_ != before;
}

bool chunk_thinner::fan_of(const std::uint32_t v, std::array<std::uint32_t, max_ring>& ring, std::size_t& count) const {
    std::array<std::uint32_t, max_ring> from{};
    std::array<std::uint32_t, max_ring> to{};
    std::size_t edges = 0;
    for (const std::uint32_t t : incident_[v]) {
        if (!triangles_[t].alive) {
            continue;
        }
        if (edges == max_ring) {
            return false;
        }
        const auto& c = triangles_[t].corner;
        const std::size_t i = c[0] == v ? 0U : c[1] == v ? 1U : 2U;
        from[edges] = c[(i + 1U) % 3U];
        to[edges] = c[(i + 2U) % 3U];
        ++edges;
    }
    if (edges < 3U) {
        return false;
    }
    std::array<bool, max_ring> used{};
    ring[0] = from[0];
    used[0] = true;
    std::uint32_t current = to[0];
    count = 1;
    while (count < edges) {
        std::size_t next = edges;
        for (std::size_t j = 0; j < edges; ++j) {
            if (!used[j] && from[j] == current) {
                next = j;
                break;
            }
        }
        if (next == edges) {
            return false;
        }
        used[next] = true;
        ring[count++] = from[next];
        current = to[next];
    }
    return current == ring[0];
}

bool chunk_thinner::free_to_collapse(const std::uint32_t v) const {
    std::array<std::uint32_t, max_ring> ring{};
    std::size_t count = 0;
    if (!fan_of(v, ring, count)) {
        return false;
    }
    const render_terrain_vertex& self = at(v);
    float winding = 0.0f;
    for (std::size_t k = 0; k < count; ++k) {
        const render_terrain_vertex& neighbour = at(ring[k]);
        if (neighbour.turf() != self.turf() || neighbour.blades() != self.blades()) {
            return false;
        }
        const float area = cross_xz(self.position, neighbour.position, position(ring[(k + 1U) % count]));
        if (area == 0.0f || (winding != 0.0f && (area < 0.0f) != (winding < 0.0f))) {
            return false;
        }
        winding = area;
    }
    return true;
}

std::size_t chunk_thinner::candidates_for(const std::uint32_t v, const std::array<std::uint32_t, max_ring>& ring, const std::size_t count,
    std::array<candidate, max_ring>& out) const {
    const glm::vec3& p = position(v);
    const float winding = cross_xz(p, position(ring[0]), position(ring[1]));
    std::size_t found_count = 0;
    for (std::size_t iu = 0; iu < count; ++iu) {
        const std::uint32_t u = ring[iu];
        candidate next{u, 0.0f, {}, {}};
        bool ok = true;
        bool found = false;
        for (std::size_t k = 0; k < count && ok; ++k) {
            const std::uint32_t a = ring[k];
            const std::uint32_t b = ring[(k + 1U) % count];
            if (a == u || b == u) {
                continue;
            }
            if (cross_xz(position(u), position(a), position(b)) * winding <= 0.0f) {
                ok = false;
            } else if (!found && barycentric(position(u), position(a), position(b), p, next.weights.data())) {
                found = true;
                next.corners = {u, a, b};
            }
        }
        if (!ok || !found) {
            continue;
        }
        next.change = std::abs(next.weights[0] * position(u).y + next.weights[1] * position(next.corners[1]).y +
                               next.weights[2] * position(next.corners[2]).y - p.y);
        out[found_count++] = next;
    }
    std::sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(found_count),
              [](const candidate& a, const candidate& b) { return a.change < b.change; });
    return found_count;
}

float chunk_thinner::cheapest_change(const std::uint32_t v) const {
    std::array<std::uint32_t, max_ring> ring{};
    std::size_t count = 0;
    std::array<candidate, max_ring> options{};
    if (!fan_of(v, ring, count)) {
        return -1.0f;
    }
    return candidates_for(v, ring, count, options) > 0U ? options[0].change : -1.0f;
}

chunk_thinner::collapse_choice chunk_thinner::evaluate(const std::uint32_t v, std::array<std::uint32_t, max_ring>& ring, std::size_t& count) const {
    collapse_choice best;
    if (!fan_of(v, ring, count)) {
        return best;
    }
    const glm::vec3& p = position(v);
    std::array<candidate, max_ring> options{};
    const std::size_t option_count = candidates_for(v, ring, count, options);
    for (std::size_t option = 0; option < option_count; ++option) {
        const candidate& choice = options[option];
        const std::uint32_t u = choice.onto;
        std::size_t iu = 0;
        while (ring[iu] != u) {
            ++iu;
        }
        const std::size_t before = (iu + count - 1U) % count;
        const std::size_t after = (iu + 1U) % count;
        bool ok = true;
        for (std::size_t k = 0; k < count && ok; ++k) {
            ok = k == iu || k == before || k == after || !adjacent(u, ring[k]);
        }
        if (!ok) {
            continue;
        }
        const float drift = std::max(drift_[u], drift_[v] + attribute_distance(at(v), blended(at(choice.corners[0]), at(choice.corners[1]),
                                                                                                at(choice.corners[2]), choice.weights.data())));
        if (drift > attribute_error_) {
            continue;
        }
        // The new edges from u cross the old ones from v, where both surfaces bend.
        float drop = choice.change;
        for (std::size_t k = 0; k < count; ++k) {
            if (k == iu || k == before || k == after) {
                continue;
            }
            const glm::vec3& from = position(u);
            const glm::vec3& to = position(ring[k]);
            for (std::size_t j = 0; j < count; ++j) {
                if (j == iu || j == k) {
                    continue;
                }
                const glm::vec3& far = position(ring[j]);
                const float rx = to.x - from.x;
                const float rz = to.z - from.z;
                const float sx = far.x - p.x;
                const float sz = far.z - p.z;
                const float denominator = rx * sz - rz * sx;
                if (std::abs(denominator) < 1.0e-12f) {
                    continue;
                }
                const float qx = p.x - from.x;
                const float qz = p.z - from.z;
                const float along_new = (qx * sz - qz * sx) / denominator;
                const float along_old = (qx * rz - qz * rx) / denominator;
                if (along_new > 1.0e-6f && along_new < 1.0f - 1.0e-6f && along_old > 1.0e-6f && along_old < 1.0f - 1.0e-6f) {
                    drop = std::max(drop, std::abs((from.y + (to.y - from.y) * along_new) - (p.y + (far.y - p.y) * along_old)));
                }
            }
        }
        const float error = drop + std::max(error_[v], error_[u]);
        best = {true, u, error, drift};
        break;
    }
    return best;
}

void chunk_thinner::collapse(const std::uint32_t v, const collapse_choice& choice, const std::array<std::uint32_t, max_ring>& ring,
    const std::size_t ring_count) {
    const std::uint32_t u = choice.onto;
    for (const std::uint32_t t : incident_[v]) {
        triangle& tri = triangles_[t];
        if (!tri.alive) {
            continue;
        }
        if (tri.corner[0] == u || tri.corner[1] == u || tri.corner[2] == u) {
            tri.alive = false;
            continue;
        }
        for (std::uint32_t& corner : tri.corner) {
            corner = corner == v ? u : corner;
        }
        incident_[u].push_back(t);
    }
    incident_[v].clear();
    alive_[v] = 0;
    // The surface round it moved by up to that much, on top of what it had.
    for (std::size_t k = 0; k < ring_count; ++k) {
        error_[ring[k]] = std::max(error_[ring[k]], choice.error);
    }
    drift_[u] = choice.drift;
    ++collapses_;
}
