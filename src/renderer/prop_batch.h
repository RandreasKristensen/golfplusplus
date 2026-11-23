#pragma once

// The one way a model is drawn: its parts (game/prop_loader.h) as instances
// of the canonical unit meshes (mesh_instance.h), for the lit prop pass
// (prop_renderer.h, assets/picture/shaders/prop.vert and prop.frag): the
// cigarette and its smoke, the can and its beer, the cart, the club. Opaque
// pieces are drawn first, those with an alpha below 1 blended over them. A
// new model file is drawn and lit with no code. GL-free.

#include "game/lighting_loader.h"
#include "game/play_area.h"
#include "game/prop_loader.h"
#include "renderer/mesh_instance.h"
#include "renderer/prop_pieces.h"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// A placed vertex as the CPU sees it (prop_batch::opaque_vertices): world
// position, world normal, colour, alpha, and the glow floor (the least light
// it shows). Never uploaded: the GPU places the canonical mesh itself.
struct prop_vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float alpha = 1.0f;
    float glow = 0.0f;
};

class prop_batch {
public:
    prop_batch();

    // Empties every list but keeps their capacity for the next frame.
    void clear();
    // Unit `mesh` placed by `model`; blended when `alpha` is below 1.
    void append(prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, float glow, float alpha = 1.0f);

    // The opaque or blended instances of one mesh, in the order they were appended.
    const std::vector<mesh_instance>& instances(bool blended, prop_mesh mesh) const { return lists_[blended ? 1 : 0][mesh_index(mesh)]; }
    // The lists joined, as uploaded: opaque cylinders, spheres, quads, then the blended ones. `joined_first`
    // is where one list starts in it.
    void join(std::vector<mesh_instance>& out) const;
    std::size_t joined_first(bool blended, prop_mesh mesh) const;
    std::size_t instance_count(bool blended) const;
    bool empty() const { return instance_count(false) + instance_count(true) == 0; }

    // Every placed vertex of the opaque or blended instances, list by list: what the draw puts on screen,
    // for tests and the lowest-point query of a rolled cart.
    std::vector<prop_vertex> opaque_vertices() const { return placed_vertices(false); }
    std::vector<prop_vertex> blended_vertices() const { return placed_vertices(true); }

private:
    static std::size_t mesh_index(prop_mesh mesh) { return static_cast<std::size_t>(mesh); }
    std::vector<prop_vertex> placed_vertices(bool blended) const;

    canonical_meshes meshes_;
    std::array<std::array<std::vector<mesh_instance>, prop_mesh_count>, 2> lists_;
};

// Vertices `append` adds for one `mesh`.
std::size_t prop_mesh_vertex_count(prop_mesh mesh);
// Vertices append_prop_model adds for `model`.
std::size_t prop_model_vertex_count(const prop_model& model);

// Every part of `model` in `frame` (its own frame in the world), each at
// the glow floor it names; a part tagged "ember" in `ember_color` when given;
// its painted parts in `paint` (their share of it) when given; tagged parts
// in their animated `pose` when given.
void append_prop_model(prop_batch& batch, const prop_model& model, const glm::mat4& frame, const glow_floors& glow,
                       const std::optional<glm::vec3>& ember_color = std::nullopt,
                       const std::optional<glm::vec3>& paint = std::nullopt,
                       const std::optional<prop_part_pose>& pose = std::nullopt);

// What a painted part of `part` shows in `paint`.
glm::vec3 painted_color(const prop_part& part, const std::optional<glm::vec3>& paint);

// How far the bus's doors have swung (0 shut, 1 open) after `elapsed` more
// seconds heading for `open`, a full swing taking `swing_seconds`. With no
// `progress` yet (a course just loaded, a join mid-boarding) they stand at it.
float door_swing_step(const std::optional<float>& progress, bool open, float elapsed, float swing_seconds);

// The hub bus's door parts (tagged "door") swung `progress` (0 shut, 1 open,
// eased) about its door_hinge point, as seen from the model's frame.
prop_part_pose bus_door_pose(const prop_model& bus, float progress);

// The model of the first placed hub bus, nullptr when there is none.
const prop_model* placed_bus(const std::vector<static_prop>& placed, const prop_catalog& props);

// Every placed prop, the bus without its doors: they move, so
// append_bus_doors draws them each frame.
void append_world_props(prop_batch& batch, const std::vector<static_prop>& placed,
    const prop_catalog& props, const glow_floors& glow);

// The placed buses' door parts swung `progress`.
void append_bus_doors(prop_batch& batch, const std::vector<static_prop>& placed, const prop_catalog& props,
                      const glow_floors& glow, float progress);

// Where `hung` goes hung at `mount` of a model whose frame is `frame`: its
// hang point at the mount, its sides along the mount's.
glm::mat4 mounted_frame(const glm::mat4& frame, const prop_mount& mount, const prop_model& hung);
