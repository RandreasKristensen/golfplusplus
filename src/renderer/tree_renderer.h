#pragma once

#include <string>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>

#include "profiling/profiling.h"
#include "renderer/frustum.h"
#include "renderer/render_mesh.h"
#include "renderer/render_tree.h"
#include "renderer/scene_haze.h"
#include "renderer/shader.h"

// Draws every course tree with two instanced draw calls: all trunks (unit
// cylinder) and all leaves (unit cone). Per-tree instance data is re-uploaded
// only when the tree revision or tree count changes; trees are static per
// course, so normal frames upload nothing.
//
// Culling: the whole batch is frustum culled against one AABB over every
// instance, computed when the instance data is uploaded. Per-chunk tree culling
// was considered and rejected: GL 3.3 glDrawArraysInstanced has no base-instance
// parameter, so contiguous visible instance ranges would need either one
// instance buffer (and draw call) per chunk, or a per-frame re-upload of the
// visible instances, both costing more than drawing every tree for the tree
// counts courses have.
//
// Owned by the renderer, which also owns the cylinder/cone vertex buffers this
// shares (they must outlive this object's VAOs being used).
class tree_renderer {
public:
    struct mesh_source {
        unsigned int vbo = 0;  // interleaved vec3 position, vec3 normal (normal unused)
        int vertex_count = 0;
    };

    bool init(const std::string& vertex_path,
              const std::string& fragment_path,
              mesh_source trunk_mesh,
              mesh_source leaf_mesh);
    void shutdown();

    // Uploads instance data if `revision`/tree count changed, then draws,
    // fading far trees into `haze`.
    // No draw calls are issued when there are no trees, or when the whole batch
    // is outside `frustum`. Returns true when the batch was drawn.
    bool draw(const std::vector<tree_body>& trees,
              std::uint64_t revision,
              const glm::mat4& view,
              const glm::mat4& proj,
              const scene_haze& haze,
              const view_frustum& frustum,
              frame_profile* profile);

private:
    struct instanced_part {
        unsigned int vao = 0;
        unsigned int instance_vbo = 0;
        int vertex_count = 0;
        std::size_t instance_count = 0;
    };

    bool init_part(instanced_part& part, mesh_source mesh);
    void shutdown_part(instanced_part& part);
    void upload_part(instanced_part& part, const std::vector<render_tree_instance>& instances, frame_profile* profile);
    void draw_part(const instanced_part& part, frame_profile* profile) const;

    shader_program shader_;
    instanced_part trunks_;
    instanced_part leaves_;
    // Bounds of every uploaded instance; rebuilt with the instance data.
    render_mesh_bounds instance_bounds_;
    std::uint64_t uploaded_revision_ = 0;
    std::size_t uploaded_tree_count_ = 0;
    bool uploaded_ = false;
};
