#pragma once

#include <vector>

#include <glm/vec3.hpp>

// GL-free unit primitives shared by the renderer's static VBOs and by the
// CPU-side world marker batch. Both sides read the same generators so the
// batched golf cart can never drift from the geometry the immediate-mode
// emote props still upload.
//
// The `_vertices` generators return interleaved position.xyz + normal.xyz
// floats, exactly the layout the terrain shader's VAOs expect. The
// `_positions` generators are the position half of that same output, which is
// all the flat-colored batch needs.

// Unit cylinder: radius 1 in XZ, spanning y in [0, 1], side quads plus both
// caps. `segments` sides.
std::vector<float> make_cylinder_vertices(int segments);

// Unit sphere: radius 1, poles on +/-y.
std::vector<float> make_sphere_vertices(int latitude_segments, int longitude_segments);

// Unit cone: radius 1 base in XZ at y = 0, tip at y = 1, plus a base disc.
std::vector<float> make_cone_vertices(int segments);

// Two triangles covering [-1, 1] in XY at z = 0, facing +Z.
std::vector<float> make_xy_quad_vertices();
// Two triangles covering [-1, 1] in XZ at y = 0, facing +Y.
std::vector<float> make_xz_quad_vertices();

// Position-only views of the generators above, in the same vertex order.
std::vector<glm::vec3> make_cylinder_positions(int segments);
std::vector<glm::vec3> make_sphere_positions(int latitude_segments, int longitude_segments);

// Strips the normals out of an interleaved position+normal vertex list.
std::vector<glm::vec3> mesh_positions_of(const std::vector<float>& interleaved);

// Tessellation shared by the GPU buffers and the CPU world marker batch so the
// two never differ.
inline constexpr int primitive_cylinder_segments = 8;
inline constexpr int primitive_sphere_latitude_segments = 8;
inline constexpr int primitive_sphere_longitude_segments = 12;
inline constexpr int primitive_cone_segments = 10;
