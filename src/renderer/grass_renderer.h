#pragma once

// Draws fixed world tufts from the course grass_field (grass_field.h),
// thinning by nested representatives and widening continuously with distance.
// The field reaches the GPU a 32 m tile at a time (grass_tile_store.h): a draw
// makes resident the tiles its visible blocks stand in, in as many batches as
// its tuning needs. Each frame, before any view, the tiles round the eye are
// fetched a few at a time (all at once when the field or the eye jumped), so
// turning and walking find their tiles resident. No camera-dependent maps or
// terrain redraws.

#include "renderer/gl_storage.h"
#include <cstdint>
#include <string>

#include <glm/mat4x4.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include "game/lighting_loader.h"
#include "profiling/profiling.h"
#include "renderer/gl_timer.h"
#include "renderer/grass_field.h"
#include "renderer/grass_tile_store.h"
#include "renderer/shader.h"

class shadow_map_pass;

// Tiles fetched ahead of the eye each frame once the set round it is resident
// (about 120 KB, within the solo upload budget).
inline constexpr int grass_prefetch_tiles = 4;

// What the blades are drawn with this frame.
struct grass_frame {
    const grass_field* field = nullptr;
    glm::mat4 view_proj{1.0f};
    glm::vec3 eye{0.0f};
    glm::vec3 rough_color{0.0f};
    glm::vec2 wind{0.0f};  // m/s, x and z
    float clock_seconds = 0.0f;
    int target_height = 1;  // pixels of the view being drawn
    hidden_disc hidden;     // blocks wholly inside it are not drawn
};

class grass_renderer {
public:
    // The first of the four texture units the field takes.
    static constexpr int field_unit = 10;
    explicit grass_renderer(gl_storage storage = {})
        : storage_(storage), blade_shader_(storage), tiles_(storage) {}

    std::uint64_t logical_bytes() const {
        return (storage_.ledger ? storage_.ledger->buffers.bytes(vbo_) : 0) + tiles_.logical_bytes();
    }
    const grass_tile_store& tiles() const { return tiles_; }
    // How many batches the last draw needed: 1 unless the tuning wants more tiles than fit.
    int last_batches() const { return last_batches_; }
    // The tiles the last draw needed resident.
    std::size_t last_tiles_wanted() const { return last_wanted_; }
    // Rebuilds the tile cache with at most `layers` layers (checks force eviction and batches with it).
    bool limit_tile_layers(int layers) { return tiles_.init(field_unit, layers); }
    std::size_t scratch_bound() const { return scratch_bound_; }
    // Frames that fetched the whole set round the eye at once: a new field or a jump.
    std::uint64_t warm_frames() const { return warm_frames_; }
    // Makes the tiles round `eye` resident ahead of the views that need them:
    // up to grass_prefetch_tiles, or all of them when the field is new or the eye
    // moved more than a tile since the last call. Call once a frame, before the views.
    void fetch_ahead(const grass_field* field, const glm::vec3& eye, const grass_style& style);
    bool init(const std::string& blade_vertex, const std::string& blade_fragment, const std::string& defines = {});
    void shutdown();
    // Uploads only the tiles it lacks, before drawing, so no blade is drawn
    // without its field. Leaves the framebuffer and viewport alone.
    void draw(const grass_frame& frame, const grass_style& style, const shadow_map_pass& shadows,
              int shadow_unit, gl_timer_pool& timers, frame_profile* profile);
private:
    std::size_t scratch_bound_ = 0;
    gl_storage storage_;
    shader_program blade_shader_;
    grass_tile_store tiles_;
    glm::vec3 last_eye_{0.0f};
    bool have_eye_ = false;
    std::uint64_t fields_seen_ = 0;
    std::uint64_t warm_frames_ = 0;
    int last_batches_ = 0;
    std::size_t last_wanted_ = 0;
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int vertex_count_ = 0;
};
