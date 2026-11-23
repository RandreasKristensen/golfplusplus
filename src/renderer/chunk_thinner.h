#pragma once

#include "renderer/render_mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <queue>
#include <vector>

#include <glm/vec3.hpp>

// The thinning behind `build_render_mesh_lod` (renderer/render_mesh_lod.h), one
// chunk at a time; the rules it keeps are described there.
// One chunk's triangles, thinned by collapsing vertices that touch no other
// chunk and no edge of the mesh.
class chunk_thinner {
public:
    static constexpr std::size_t max_ring = 12;
    static constexpr std::uint32_t none = 0xffffffffu;

    struct collapse_choice {
        bool valid = false;
        std::uint32_t onto = none;
        float error = 0.0f;
        float drift = 0.0f;
    };

    explicit chunk_thinner(const std::vector<render_terrain_vertex>& vertices, const float attribute_error)
        : vertices_(vertices), attribute_error_(attribute_error) {}

    // False when an index is out of range (the chunk is left whole).
    bool load(const std::vector<std::uint32_t>& indices, const render_mesh_chunk& chunk, const std::vector<std::uint8_t>& shared,
              std::vector<std::uint32_t>& local_of);

    // Collapses while the cheapest collapse stays within `error_meters`.
    // Returns whether anything was collapsed since the last call.
    bool thin_to(const float error_meters);

    void append_alive(std::vector<std::uint32_t>& out) const {
        for (const triangle& t : triangles_) {
            if (t.alive) {
                for (const std::uint32_t corner : t.corner) {
                    out.push_back(global_[corner]);
                }
            }
        }
    }

private:
    struct triangle {
        std::array<std::uint32_t, 3> corner{};
        bool alive = true;
    };
    struct entry {
        float key;
        std::uint32_t vertex;
        bool operator>(const entry& other) const { return key > other.key; }
    };

    void release(std::vector<std::uint32_t>& local_of) const {
        for (const std::uint32_t g : global_) {
            local_of[g] = none;
        }
    }

    const render_terrain_vertex& at(const std::uint32_t v) const { return vertices_[global_[v]]; }
    const glm::vec3& position(const std::uint32_t v) const { return vertices_[global_[v]].position; }

    // The neighbours of `v` in order round it, false unless every edge at it has two triangles.
    bool fan_of(const std::uint32_t v, std::array<std::uint32_t, max_ring>& ring, std::size_t& count) const;

    // Free: a closed fan of triangles of one winding, the same turf and
    // blades as every neighbour.
    bool free_to_collapse(const std::uint32_t v) const;

    bool adjacent(const std::uint32_t a, const std::uint32_t b) const {
        for (const std::uint32_t t : incident_[a]) {
            const auto& c = triangles_[t].corner;
            if (triangles_[t].alive && (c[0] == b || c[1] == b || c[2] == b)) {
                return true;
            }
        }
        return false;
    }

    // Where each neighbour of `v` could take it: the new triangles must keep
    // their winding, and the surface moves at `v` by `change`. Cheapest first.
    struct candidate {
        std::uint32_t onto;
        float change;
        std::array<std::uint32_t, 3> corners;
        std::array<float, 3> weights;
    };

    std::size_t candidates_for(const std::uint32_t v, const std::array<std::uint32_t, max_ring>& ring, const std::size_t count,
                               std::array<candidate, max_ring>& out) const;

    // The cheapest the surface moves at `v` if it collapsed, without the exact tests.
    float cheapest_change(const std::uint32_t v) const;

    // The cheapest collapse of `v` onto a neighbour that joins no vertices that
    // were not neighbours, stays within the attribute error and does not move
    // the surface at any crossing of the new edges with the old ones by more
    // than the error it adds.
    collapse_choice evaluate(const std::uint32_t v, std::array<std::uint32_t, max_ring>& ring, std::size_t& count) const;

    void push(const std::uint32_t v) {
        if (alive_[v] && !locked_[v]) {
            const float change = cheapest_change(v);
            if (change >= 0.0f) {
                heap_.push({change + error_[v], v});
            }
        }
    }

    void collapse(const std::uint32_t v, const collapse_choice& choice, const std::array<std::uint32_t, max_ring>& ring,
                  const std::size_t ring_count);

    const std::vector<render_terrain_vertex>& vertices_;
    float attribute_error_;
    std::vector<std::uint32_t> global_;
    std::vector<triangle> triangles_;
    std::vector<std::vector<std::uint32_t>> incident_;
    std::vector<std::uint8_t> alive_;
    std::vector<std::uint8_t> locked_;
    std::vector<float> error_;
    std::vector<float> drift_;
    std::priority_queue<entry, std::vector<entry>, std::greater<entry>> heap_;
    std::size_t collapses_ = 0;
};

