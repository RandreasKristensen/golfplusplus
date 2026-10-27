#pragma once

// Authored surface zones (green, bunker, water, fairway) that shape and
// colour the terrain, each its mapped outline, and the one test of where a
// point sits in one: everything that asks whether a point is in a zone, or
// how far in, goes through here. Edge blends (a bunker's lip, a pond's bank)
// fall off with distance from the edge in metres. Pure.

#include <array>
#include <optional>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

enum class material_zone_type {
    green,
    bunker,
    water,
    fairway,
    unknown  // unrecognised type in the data; ignored by terrain building
};

// A zone is its outline on the ground: points (x, z) round its edge, either
// way round, the last joined back to the first, the edge never crossing
// itself. `center` and `half_size` are its box's (y 0), kept with it.
struct material_zone {
    material_zone_type type = material_zone_type::unknown;
    std::vector<glm::vec2> outline;
    glm::vec3 center{0.0f};
    glm::vec2 half_size{0.0f};
};

// The zone of `type` round `outline`, its box worked out.
material_zone make_material_zone(material_zone_type type, std::vector<glm::vec2> outline);

// Whether `position` (ignoring height) is inside the outline.
bool zone_contains(const material_zone& zone, const glm::vec3& position);

// How far (metres, ignoring height) `position` is from the outline's edge,
// inside or out.
float zone_edge_distance(const material_zone& zone, const glm::vec3& position);

// How far inside the zone `position` is: its distance from the edge; nullopt
// outside.
std::optional<float> zone_depth_inside(const material_zone& zone, const glm::vec3& position);

// How far beyond the zone's edge `position` is: 0 inside.
float zone_distance_outside(const material_zone& zone, const glm::vec3& position);

// Half the size of the zone along world x and z: the box around it (about `center`).
inline glm::vec2 zone_half_extent(const material_zone& zone) {
    return zone.half_size;
}

// A piece of an edge: its two ends.
using zone_edge = std::array<glm::vec2, 2>;

// The pieces of `zone`'s edge outside all of `others`: where it is the edge
// of the ground they cover together.
std::vector<zone_edge> edge_outside(const material_zone& zone, const std::vector<const material_zone*>& others);

// How far (metres, ignoring height) `position` is from the nearest of `edges`.
float edges_distance(const std::vector<zone_edge>& edges, const glm::vec3& position);

// The outline's area in square metres.
float zone_area(const material_zone& zone);

// The outline cut into triangles that cover it exactly (ear clipping), each
// its three corners.
std::vector<std::array<glm::vec2, 3>> zone_triangles(const material_zone& zone);
