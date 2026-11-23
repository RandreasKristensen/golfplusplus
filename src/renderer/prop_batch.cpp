#include "renderer/prop_batch.h"

#include "game/lighting_fields.h"
#include "physics/vector_math.h"
#include "renderer/camera_local.h"
#include "renderer/primitive_mesh.h"

#include <algorithm>
#include <iterator>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/mat3x3.hpp>
#include <glm/matrix.hpp>
#include <glm/vec4.hpp>

namespace {
float glow_floor(const glow_floors& glow, const std::string& name) {
    for (const share_field<glow_floors>& field : glow_fields) {
        if (name == field.key) {
            return glow.*field.member;
        }
    }
    return 0.0f;
}
}

prop_batch::prop_batch() = default;

void prop_batch::clear() {
    for (auto& list : lists_) {
        for (std::vector<mesh_instance>& instances : list) { instances.clear(); }
    }
}

void prop_batch::append(const prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, const float glow,
                        const float alpha) {
    lists_[alpha < 1.0f ? 1 : 0][mesh_index(mesh)].push_back(make_mesh_instance(model, color, glow, alpha));
}

std::size_t prop_batch::instance_count(const bool blended) const {
    std::size_t count = 0;
    for (const std::vector<mesh_instance>& instances : lists_[blended ? 1 : 0]) { count += instances.size(); }
    return count;
}

std::size_t prop_batch::joined_first(const bool blended, const prop_mesh mesh) const {
    std::size_t first = blended ? instance_count(false) : 0;
    for (std::size_t m = 0; m < mesh_index(mesh); ++m) { first += lists_[blended ? 1 : 0][m].size(); }
    return first;
}

void prop_batch::join(std::vector<mesh_instance>& out) const {
    out.clear();
    for (const auto& list : lists_) {
        for (const std::vector<mesh_instance>& instances : list) { out.insert(out.end(), instances.begin(), instances.end()); }
    }
}

std::vector<prop_vertex> prop_batch::placed_vertices(const bool blended) const {
    std::vector<prop_vertex> vertices;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        for (const mesh_instance& instance : instances(blended, mesh)) {
            for (std::size_t i = 0; i < meshes_.vertex_count(mesh); ++i) {
                prop_vertex vertex;
                vertex.position = placed_position(instance, meshes_, mesh, i);
                vertex.normal = placed_normal(instance, meshes_, mesh, i);
                vertex.color = instance.color;
                vertex.alpha = instance.alpha;
                vertex.glow = instance.glow;
                vertices.push_back(vertex);
            }
        }
    }
    return vertices;
}

std::size_t prop_mesh_vertex_count(const prop_mesh mesh) {
    return canonical_meshes().vertex_count(mesh);
}

std::size_t prop_model_vertex_count(const prop_model& model) {
    std::size_t count = 0;
    for (const prop_mesh mesh : {prop_mesh::cylinder, prop_mesh::sphere, prop_mesh::quad}) {
        count += prop_piece_count(model, mesh) * prop_mesh_vertex_count(mesh);
    }
    return count;
}

glm::vec3 painted_color(const prop_part& part, const std::optional<glm::vec3>& paint) {
    return paint && part.paint_share ? glm::min(*paint * *part.paint_share, glm::vec3(1.0f)) : part.color;
}

void append_prop_model(prop_batch& batch, const prop_model& model, const glm::mat4& frame, const glow_floors& glow,
                       const std::optional<glm::vec3>& ember_color, const std::optional<glm::vec3>& paint,
                       const std::optional<prop_part_pose>& pose) {
    for (const prop_piece& piece : prop_pieces(model, frame, pose)) {
        const bool ember = ember_color && piece.part->tag == "ember";
        const glm::vec3 color = ember ? *ember_color : painted_color(*piece.part, paint);
        batch.append(piece.mesh, piece.model, color, glow_floor(glow, piece.part->glow));
    }
}

glm::mat4 mounted_frame(const glm::mat4& frame, const prop_mount& mount, const prop_model& hung) {
    return frame * posed_model(mount.at, mount.up, mount.forward, glm::vec3(1.0f)) *
           glm::translate(glm::mat4(1.0f), -hung.point("hang"));
}

float door_swing_step(const std::optional<float>& progress, const bool open, const float elapsed, const float swing_seconds) {
    if (!progress) { return open ? 1.0f : 0.0f; }
    const float step = swing_seconds > 0.0f ? std::max(elapsed, 0.0f) / swing_seconds : 1.0f;
    return glm::clamp(*progress + (open ? step : -step), 0.0f, 1.0f);
}

prop_part_pose bus_door_pose(const prop_model& bus, const float progress) {
    const float eased = glm::smoothstep(0.0f, 1.0f, progress);
    const glm::vec3 hinge = bus.point("door_hinge");
    return prop_part_pose{"door", glm::translate(glm::mat4(1.0f), hinge) *
        glm::rotate(glm::mat4(1.0f), glm::radians(bus.number("door_open_degrees")) * eased, glm::vec3(0.0f, 1.0f, 0.0f)) *
        glm::translate(glm::mat4(1.0f), -hinge)};
}

const prop_model* placed_bus(const std::vector<static_prop>& placed, const prop_catalog& props) {
    for (const auto& prop : placed) {
        const prop_model& model = props.at(prop.model);
        if (model.kind == "hub_bus") { return &model; }
    }
    return nullptr;
}

void append_world_props(prop_batch& batch, const std::vector<static_prop>& placed,
    const prop_catalog& props, const glow_floors& glow) {
    for (const auto& prop : placed) {
        const prop_model& model = props.at(prop.model);
        const glm::mat4 frame = posed_model(prop.position, world_up, yaw_direction(prop.heading), glm::vec3(1));
        if (model.kind != "hub_bus") {
            append_prop_model(batch, model, frame, glow);
            continue;
        }
        for (const prop_piece& piece : prop_pieces(model, frame)) {
            if (piece.part->tag != "door") { batch.append(piece.mesh, piece.model, piece.part->color, glow_floor(glow, piece.part->glow)); }
        }
    }
}

void append_bus_doors(prop_batch& batch, const std::vector<static_prop>& placed, const prop_catalog& props,
                      const glow_floors& glow, const float progress) {
    for (const auto& prop : placed) {
        const prop_model& model = props.at(prop.model);
        if (model.kind != "hub_bus") { continue; }
        const glm::mat4 frame = posed_model(prop.position, world_up, yaw_direction(prop.heading), glm::vec3(1));
        for (const prop_piece& piece : prop_pieces(model, frame, bus_door_pose(model, progress))) {
            if (piece.part->tag == "door") { batch.append(piece.mesh, piece.model, piece.part->color, glow_floor(glow, piece.part->glow)); }
        }
    }
}
