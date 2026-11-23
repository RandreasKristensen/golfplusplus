#include "renderer/mesh_instance.h"

#include "physics/vector_math.h"
#include "renderer/primitive_mesh.h"

#include <glm/geometric.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

mesh_instance make_mesh_instance(const glm::mat4& model, const glm::vec3& color, const float glow, const float alpha) {
    mesh_instance instance;
    instance.axis_x = glm::vec3(model[0]);
    instance.axis_y = glm::vec3(model[1]);
    instance.axis_z = glm::vec3(model[2]);
    instance.origin = glm::vec3(model[3]);
    instance.color = color;
    instance.alpha = alpha;
    instance.glow = glow;
    return instance;
}

glm::mat4 instance_matrix(const mesh_instance& instance) {
    return glm::mat4(glm::vec4(instance.axis_x, 0.0f), glm::vec4(instance.axis_y, 0.0f), glm::vec4(instance.axis_z, 0.0f),
                     glm::vec4(instance.origin, 1.0f));
}

canonical_meshes::canonical_meshes() {
    meshes_[static_cast<std::size_t>(prop_mesh::cylinder)] = make_cylinder_vertices(primitive_cylinder_segments);
    meshes_[static_cast<std::size_t>(prop_mesh::sphere)] =
        make_sphere_vertices(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments);
    meshes_[static_cast<std::size_t>(prop_mesh::quad)] = make_xy_quad_vertices();
}

glm::vec3 placed_position(const mesh_instance& instance, const canonical_meshes& meshes, const prop_mesh mesh, const std::size_t index) {
    const float* at = &meshes.vertices(mesh)[index * canonical_meshes::floats_per_vertex];
    return glm::vec3(instance_matrix(instance) * glm::vec4(at[0], at[1], at[2], 1.0f));
}

std::array<glm::vec3, 8> placed_corners(const mesh_instance& instance, const prop_mesh mesh) {
    // The meshes' extents (primitive_mesh.h): a cylinder's y runs 0 to 1, a quad has no depth.
    const float low_y = mesh == prop_mesh::cylinder ? 0.0f : -1.0f;
    const float z = mesh == prop_mesh::quad ? 0.0f : 1.0f;
    const glm::mat4 model = instance_matrix(instance);
    std::array<glm::vec3, 8> corners{};
    for (std::size_t i = 0; i < corners.size(); ++i) {
        const glm::vec3 local((i & 1U) != 0U ? 1.0f : -1.0f, (i & 2U) != 0U ? 1.0f : low_y, (i & 4U) != 0U ? z : -z);
        corners[i] = glm::vec3(model * glm::vec4(local, 1.0f));
    }
    return corners;
}

glm::vec3 placed_normal(const mesh_instance& instance, const canonical_meshes& meshes, const prop_mesh mesh, const std::size_t index) {
    const float* at = &meshes.vertices(mesh)[index * canonical_meshes::floats_per_vertex];
    const glm::mat3 normals = glm::transpose(glm::inverse(glm::mat3(instance_matrix(instance))));
    return safe_normalize(normals * glm::vec3(at[3], at[4], at[5]), world_up);
}
