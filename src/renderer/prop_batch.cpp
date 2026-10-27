#include "renderer/prop_batch.h"

#include "game/lighting_fields.h"
#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"

#include <iterator>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

namespace {
constexpr std::size_t floats_per_vertex = 6;

float glow_floor(const glow_floors& glow, const std::string& name) {
    for (const share_field<glow_floors>& field : glow_fields) {
        if (name == field.key) {
            return glow.*field.member;
        }
    }
    return 0.0f;
}
}

prop_batch::prop_batch()
    : cylinder_(make_cylinder_vertices(primitive_cylinder_segments)),
      sphere_(make_sphere_vertices(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments)),
      quad_(make_xy_quad_vertices()) {}

void prop_batch::clear() {
    opaque_.clear();
    blended_.clear();
}

void prop_batch::append(const prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, const float glow,
                        const float alpha) {
    const std::vector<float>& unit = mesh == prop_mesh::cylinder ? cylinder_ : mesh == prop_mesh::sphere ? sphere_ : quad_;
    std::vector<prop_vertex>& out = alpha < 1.0f ? blended_ : opaque_;
    const glm::mat3 normals = glm::transpose(glm::inverse(glm::mat3(model)));
    for (std::size_t i = 0; i + floats_per_vertex <= unit.size(); i += floats_per_vertex) {
        prop_vertex vertex;
        vertex.position = glm::vec3(model * glm::vec4(unit[i], unit[i + 1], unit[i + 2], 1.0f));
        vertex.normal = safe_normalize(normals * glm::vec3(unit[i + 3], unit[i + 4], unit[i + 5]), world_up);
        vertex.color = color;
        vertex.alpha = alpha;
        vertex.glow = glow;
        out.push_back(vertex);
    }
}

std::size_t prop_mesh_vertex_count(const prop_mesh mesh) {
    switch (mesh) {
    case prop_mesh::cylinder:
        return make_cylinder_vertices(primitive_cylinder_segments).size() / floats_per_vertex;
    case prop_mesh::sphere:
        return make_sphere_vertices(primitive_sphere_latitude_segments, primitive_sphere_longitude_segments).size() /
               floats_per_vertex;
    case prop_mesh::quad:
        return make_xy_quad_vertices().size() / floats_per_vertex;
    }
    return 0;
}

std::size_t prop_model_vertex_count(const prop_model& model) {
    std::size_t count = 0;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        count += prop_piece_count(model, mesh) * prop_mesh_vertex_count(mesh);
    }
    return count;
}

void append_prop_model(prop_batch& batch, const prop_model& model, const glm::mat4& frame, const glow_floors& glow,
                       const std::optional<glm::vec3>& ember_color) {
    for (const prop_piece& piece : prop_pieces(model, frame)) {
        const bool ember = ember_color && piece.part->tag == "ember";
        batch.append(piece.mesh, piece.model, ember ? *ember_color : piece.color, glow_floor(glow, piece.part->glow));
    }
}

glm::mat4 mounted_frame(const glm::mat4& frame, const prop_mount& mount, const prop_model& hung) {
    return frame * posed_model(mount.at, mount.up, mount.forward, glm::vec3(1.0f)) *
           glm::translate(glm::mat4(1.0f), -hung.point("hang"));
}
