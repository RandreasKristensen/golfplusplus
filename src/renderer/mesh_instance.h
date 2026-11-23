#pragma once

// A piece of a model or a figure, placed: one of the three canonical meshes
// (a unit cylinder, sphere or quad: primitive_mesh.h) and the affine matrix
// that sets it in the world, with its colour, alpha and glow floor. The
// crowd (carts, bags, gear, figures, balls) is drawn as lists of these: the
// canonical meshes are uploaded once (instance_meshes.h) and a draw places
// them from the instance buffer, so no placed vertex is built or uploaded
// per frame. The functions below give the placed vertices on the CPU, for
// tests and the one geometry query (a rolled cart's lowest point). GL-free.

#include <array>
#include <cstddef>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

enum class prop_mesh {
    cylinder,  // unit cylinder, y in [0, 1]
    sphere,    // unit sphere
    quad       // unit quad, [-1, 1] in XY
};
inline constexpr std::size_t prop_mesh_count = 3;

// Uploaded straight into a GL buffer: the matrix's columns (its bottom row
// is 0, 0, 0, 1), then colour and alpha, then the glow floor (a prop's slot
// when below zero: renderer/prop_glow.h).
struct mesh_instance {
    glm::vec3 axis_x{1.0f, 0.0f, 0.0f};
    glm::vec3 axis_y{0.0f, 1.0f, 0.0f};
    glm::vec3 axis_z{0.0f, 0.0f, 1.0f};
    glm::vec3 origin{0.0f};
    glm::vec3 color{1.0f};
    float alpha = 1.0f;
    float glow = 0.0f;
};

mesh_instance make_mesh_instance(const glm::mat4& model, const glm::vec3& color, float glow, float alpha = 1.0f);
glm::mat4 instance_matrix(const mesh_instance& instance);

// The three meshes as interleaved position.xyz + normal.xyz, in the order the
// batches' vertex lists once had them (a test or the GL side builds one).
class canonical_meshes {
public:
    canonical_meshes();
    const std::vector<float>& vertices(prop_mesh mesh) const { return meshes_[static_cast<std::size_t>(mesh)]; }
    std::size_t vertex_count(prop_mesh mesh) const { return vertices(mesh).size() / floats_per_vertex; }
    static constexpr std::size_t floats_per_vertex = 6;

private:
    std::vector<float> meshes_[prop_mesh_count];
};

// Vertex `index` of `mesh` placed by `instance`: its world position, and its
// normal turned by the matrix's inverse transpose and normalized (up when the
// matrix has none).
glm::vec3 placed_position(const mesh_instance& instance, const canonical_meshes& meshes, prop_mesh mesh, std::size_t index);
glm::vec3 placed_normal(const mesh_instance& instance, const canonical_meshes& meshes, prop_mesh mesh, std::size_t index);

// The corners of `mesh`'s bounding box placed by `instance` (a quad's four, twice). How far a vertex
// moves between two placements is linear in where it lies on the mesh, so the farthest any vertex
// moves is at a corner of any box round the mesh: tracking these tracks every vertex, never less.
std::array<glm::vec3, 8> placed_corners(const mesh_instance& instance, prop_mesh mesh);
