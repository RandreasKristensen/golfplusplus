#pragma once

// The ground's two static meshes, the terrain and the material overlay drawn
// over it, uploaded only when their revision changes (the terrain's coarse
// index lists with it, in a second buffer) and drawn frustum-culled
// in index-ordered ranges, joined across gaps a draw call costs more than, with the terrain shader. Also the shader's
// per-draw state and the vertex layout every render_terrain_vertex buffer
// shares (the flight path's too).

#include "renderer/gl_storage.h"
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

// How the material overlay meets the ground under it: pulled forward in
// depth (a view at an angle), or drawn over whatever is there (a view from
// straight above, where depth cannot tell them apart).
enum class overlay_depth { pulled_forward, over_all };

class terrain_renderer {
public:
    explicit terrain_renderer(gl_storage storage = {})
        : storage_(storage) {}

    bool init();
    void shutdown();
    // Uploads whichever mesh changed, then draws what `frustum` sees with
    // `shader`, already in use. Null meshes draw nothing. The terrain's chunks
    // are drawn as the coarser levels `lod` accepts; the overlay never is.
    terrain_cull_stats draw(const shader_program& shader, const render_static_mesh* terrain,
                            const render_static_mesh* material_overlay, const glm::mat4& view_proj,
                            const view_frustum& frustum, overlay_depth overlay, const render_lod_view& lod,
                            frame_profile* profile);

private:
    gl_storage storage_;
    struct static_mesh_buffers {
        unsigned int vao = 0;
        unsigned int vbo = 0;
        unsigned int ebo = 0;
        // The coarse index lists, with their own vertex array on the same vertices.
        unsigned int lod_vao = 0;
        unsigned int lod_ebo = 0;
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
