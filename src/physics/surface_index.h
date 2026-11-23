#pragma once

// Which holes and zones can matter at a point: a uniform XZ grid over their
// boxes, so a surface query looks at the few that reach its cell instead of
// all of them. Purely an acceleration structure owned by the area: a query
// through it gives the same result as scanning every hole and zone in order
// (each cell lists indices ascending), and an index built for other counts
// falls back to that scan.

#include "physics/ground_mesh.h"
#include "physics/material_zone.h"
#include "physics/terrain.h"

#include <cstdint>
#include <optional>
#include <vector>

#include <glm/vec3.hpp>

struct surface_index {
    float min_x = 0.0f;
    float min_z = 0.0f;
    float cell_size = 1.0f;
    int columns = 0;
    int rows = 0;
    std::size_t hole_count = 0;
    std::size_t zone_count = 0;
    std::vector<std::uint32_t> hole_starts;  // columns * rows + 1, into hole_items
    std::vector<std::uint32_t> hole_items;
    std::vector<std::uint32_t> zone_starts;
    std::vector<std::uint32_t> zone_items;
};

surface_index build_surface_index(const std::vector<terrain_mesh>& holes, const std::vector<material_zone>& zones);

// The surface queries of ground_mesh.h and terrain.h through the index: the
// same results as their scans over every hole and zone.
std::optional<terrain_sample> sample_holes(const std::vector<terrain_mesh>& holes, const surface_index& index,
                                           const glm::vec3& position);
std::optional<zone_hit> winning_zone_at(const std::vector<material_zone>& zones, const surface_index& index,
                                        const glm::vec3& position);
