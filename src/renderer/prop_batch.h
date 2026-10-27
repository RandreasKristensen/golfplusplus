#pragma once

// The one way a model is drawn: its parts (game/prop_loader.h) as unit
// meshes with their normals, in the world, for the lit prop pass
// (prop_renderer.h, assets/picture/shaders/prop.frag): the cigarette and its
// smoke, the can and its beer, the cart, the club. Opaque pieces are drawn
// first, those with an alpha below 1 blended over them. A new model file is
// drawn and lit with no code. GL-free.

#include "game/lighting_loader.h"
#include "game/prop_loader.h"
#include "renderer/prop_pieces.h"

#include <cstddef>
#include <optional>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Uploaded straight into a GL buffer: world position, world normal, colour,
// alpha, and the glow floor (the least light it shows).
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

    // Empties both lists but keeps their capacity for the next frame.
    void clear();
    // Unit `mesh` placed by `model`; blended when `alpha` is below 1.
    void append(prop_mesh mesh, const glm::mat4& model, const glm::vec3& color, float glow, float alpha = 1.0f);

    const std::vector<prop_vertex>& opaque() const { return opaque_; }
    const std::vector<prop_vertex>& blended() const { return blended_; }
    bool empty() const { return opaque_.empty() && blended_.empty(); }

private:
    // Interleaved position.xyz + normal.xyz (primitive_mesh.h).
    std::vector<float> cylinder_;
    std::vector<float> sphere_;
    std::vector<float> quad_;
    std::vector<prop_vertex> opaque_;
    std::vector<prop_vertex> blended_;
};

// Vertices `append` adds for one `mesh`.
std::size_t prop_mesh_vertex_count(prop_mesh mesh);
// Vertices append_prop_model adds for `model`.
std::size_t prop_model_vertex_count(const prop_model& model);

// Every part of `model` in `frame` (its own frame in the world), each at
// the glow floor it names; a part tagged "ember" in `ember_color` when given.
void append_prop_model(prop_batch& batch, const prop_model& model, const glm::mat4& frame, const glow_floors& glow,
                       const std::optional<glm::vec3>& ember_color = std::nullopt);

// Where `hung` goes hung at `mount` of a model whose frame is `frame`: its
// hang point at the mount, its sides along the mount's.
glm::mat4 mounted_frame(const glm::mat4& frame, const prop_mount& mount, const prop_model& hung);
