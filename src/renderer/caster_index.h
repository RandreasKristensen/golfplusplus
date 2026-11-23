#pragma once

// The immutable triangles that cast shadows (hole signs, fence posts), sorted
// into square tiles once per course so a frame draws only the runs of tiles
// within reach of the camera, from a GPU buffer filled once. A tile's bounds
// hold every corner of its triangles, so a triangle with a corner within
// reach is always in a selected run (others near it come along). GL-free.

#include <cstddef>
#include <vector>

#include <glm/vec3.hpp>

struct caster_run {
    std::size_t first = 0;  // vertices
    std::size_t count = 0;
};

class caster_index {
public:
    // Tile side in metres.
    static constexpr float tile_meters = 64.0f;

    // Sorts `corners` (three per triangle, a trailing partial triangle dropped)
    // by tile and returns them in that order: the buffer to upload.
    std::vector<glm::vec3> build(const std::vector<glm::vec3>& corners);
    // The merged runs of tiles with a corner possibly within `reach` metres
    // horizontally of `eye`.
    const std::vector<caster_run>& select(const glm::vec3& eye, float reach);

    std::size_t vertex_count() const { return vertex_count_; }

private:
    struct tile {
        glm::vec3 low{0.0f};
        glm::vec3 high{0.0f};
        std::size_t first = 0;
        std::size_t count = 0;
    };
    std::vector<tile> tiles_;
    std::vector<caster_run> runs_;
    std::size_t vertex_count_ = 0;
};
