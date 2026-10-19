#pragma once

// The ground's two static meshes, the terrain and the material overlay drawn
// over it, uploaded only when their revision changes and drawn frustum-culled
// in a few merged index ranges with the terrain shader. Also the shader's
// per-draw state and the vertex layout every render_terrain_vertex buffer
// shares (the flight path's too).

#include "profiling/profiling.h"
#include "renderer/frustum.h"
#include "renderer/render_mesh.h"
#include "renderer/render_mesh_chunks.h"
#include "renderer/shader.h"

#include <cstdint>
#include <vector>

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

// Frustum culling result for the static scene meshes, for the profiler.
struct terrain_cull_stats {
    render_chunk_cull_stats terrain;
    render_chunk_cull_stats material_overlay;
};

// Describes render_terrain_vertex into the bound VAO from the bound array buffer.
void describe_render_terrain_vertex();

// Flat (unlit, with its glow floor) or vertex-coloured (lit) draw with the
// terrain shader.
void set_terrain_draw_state(const shader_program& shader, const glm::mat4& model, const glm::mat4& view_proj,
                            const glm::vec3& color, float alpha, float glow, bool use_vertex_color);

class terrain_renderer {
public:
    bool init();
    void shutdown();
    // Uploads whichever mesh changed, then draws what `frustum` sees with
    // `shader`, already in use. Null meshes draw nothing.
    terrain_cull_stats draw(const shader_program& shader, const render_static_mesh* terrain,
                            const render_static_mesh* material_overlay, const glm::mat4& view_proj,
                            const view_frustum& frustum, frame_profile* profile);

private:
    struct static_mesh_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        int index_count = 0;
        std::uint64_t uploaded_revision = 0;
        bool uploaded = false;
    };

    void upload(static_mesh_buffers& buffers, const render_static_mesh* mesh, frame_profile* profile);

    static_mesh_buffers terrain_;
    static_mesh_buffers material_overlay_;
    std::vector<render_index_range> terrain_ranges_;
    std::vector<render_index_range> material_overlay_ranges_;
};
