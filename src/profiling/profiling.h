#pragma once

// Per-frame wall times, GPU timestamps, submitted work and resident resources.
// The app publishes the originating frame once delayed timestamps arrive.

#include "profiling/frame_accounting.h"
#include <array>
#include <functional>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

enum class profile_stage : std::size_t {
    update_game = 0,
    refresh_render_mesh_cache,
    make_render_data,
    render,
    render_scene,
    render_overlay,
    render_crt,
    window_swap,
    query_read,
    upload,
    readback,
    count
};

inline constexpr std::size_t profile_stage_count = static_cast<std::size_t>(profile_stage::count);

enum class gpu_profile_stage : std::size_t {
    shadow_map = 0,
    terrain,
    grass,  // the rough's fixed world tufts (grass_renderer)
    shadows,
    trees,
    props,  // every model: carts, clubs, what is held (prop_renderer)
    sky,
    reflection, // the real mirrored scene, once per visible water level
    water,  // the ponds: a copy of the scene under them and their surfaces, or the picture seen from under one (water_renderer)
    weather,
    shafts,  // the sun's shafts: shadows taken out of the haze (sun_shaft_pass)
    lamp_shadows,  // the floodlights' shadow maps, including the casters' wind pose (lamp_shadow_pass)
    eyepiece,
    zoom,  // the eyepiece's view, the scene drawn again at its zoom
    bloom,
    smear,
    camcorder,
    tape,
    overlay,
    crt,
    count
};

inline constexpr std::size_t gpu_profile_stage_count = static_cast<std::size_t>(gpu_profile_stage::count);

enum class gpu_stage_group : std::size_t { scene, reflection, zoom, zoom_reflection, count };
using gpu_stage_times = std::array<double, gpu_profile_stage_count>;
using gpu_view_times = std::array<gpu_stage_times, static_cast<std::size_t>(gpu_stage_group::count)>;

// Plain data. Zero-initialised means "nothing recorded".
struct frame_profile {
    std::uint64_t frame_number = 0;
    frame_accounting work;
    process_memory memory;
    std::uint64_t gpu_buffer_bytes = 0;
    std::uint64_t gpu_texture_bytes = 0;
    std::uint64_t gpu_renderbuffer_bytes = 0;
    double course_load_ms = 0;
    double course_bake_ms = 0;
    double gpu_frame_ms = 0;
    // Children of a reflection or zoom have their own columns. The parent is inclusive.
    gpu_view_times gpu_ms{};
    double stage_ms[profile_stage_count] = {};
    bool gpu_timers_available = false;

    std::uint32_t terrain_sample_calls = 0;
    std::uint64_t terrain_triangles_tested = 0;
    std::uint32_t draw_calls = 0;
    std::uint32_t uniform_sets = 0;
    // Actual glGetUniformLocation calls (uniform location cache misses).
    std::uint32_t uniform_location_queries = 0;
    // glBufferData calls: GPU storage (re)allocations, with or without data.
    std::uint32_t buffer_reallocations = 0;
    // glBufferSubData calls: streaming writes into existing storage.
    std::uint32_t buffer_writes = 0;
    // Bytes of vertex/index data sent by either call.
    std::uint64_t buffer_upload_bytes = 0;
    // GL work spent on the debug overlay itself, kept out of the counters
    // above so the numbers describe the real frame.
    std::uint32_t debug_overlay_draw_calls = 0;
    std::uint32_t debug_overlay_uniform_sets = 0;
    // Buffer calls (writes + reallocations) and bytes of the debug text.
    std::uint32_t debug_overlay_buffer_calls = 0;
    std::uint64_t debug_overlay_buffer_bytes = 0;
    // Frustum culling of the chunked static meshes (terrain + material overlay
    // summed) from the main scene.
    std::uint32_t visible_chunks = 0;
    std::uint32_t culled_chunks = 0;
    std::uint32_t chunk_draw_ranges = 0;
    std::uint64_t chunk_indices_drawn = 0;
    std::uint64_t chunk_indices_total = 0;
    // False when the whole tree batch was culled (both instanced draws skipped).
    bool trees_visible = false;

    float frame_ms = 0.0f;
};

using frame_profile_sink = std::function<void(const frame_profile&)>;

struct profiler {
    std::uint64_t next_frame = 0;
    bool enabled = false;
    float publish_interval_seconds = 0.25f;

    frame_profile frame;      // being recorded right now
    frame_profile published;  // averaged over the last publish window
    bool has_published = false;

    frame_profile accumulator;
    int accumulated_frames = 0;
    int accumulated_gpu_frames = 0;
    float accumulated_seconds = 0.0f;
};

// Start an empty, numbered record, or return null when recording is disabled.
frame_profile* profiler_begin_frame(profiler& profile);
// Fold a completed record into the overlay's publishing window.
void publish_profile(profiler& profile, const frame_profile& frame);

inline void record_draw_call(frame_profile* profile, const std::uint32_t count = 1) {
    if (profile != nullptr) {
        profile->draw_calls += count;
    }
}

inline void record_uniform_set(frame_profile* profile, const std::uint32_t count = 1) {
    if (profile != nullptr) {
        profile->uniform_sets += count;
    }
}

inline void record_uniform_location_query(frame_profile* profile) {
    if (profile != nullptr) {
        profile->uniform_location_queries += 1U;
    }
}

// glGet* and glIsEnabled calls that read state back from the driver.
inline void record_state_readbacks(frame_profile* profile, const std::uint32_t count) {
    if (profile != nullptr) {
        profile->work.state_readbacks += count;
    }
}

// One glBufferData: (re)allocates storage and uploads `bytes` of data (0 for
// an orphan with nullptr).
inline void record_buffer_upload(frame_profile* profile, const std::size_t bytes) {
    if (profile != nullptr) {
        profile->buffer_reallocations += 1U;
        profile->buffer_upload_bytes += static_cast<std::uint64_t>(bytes);
    }
}

// One glBufferSubData streaming `bytes` into existing storage.
inline void record_buffer_write(frame_profile* profile, const std::size_t bytes) {
    if (profile != nullptr) {
        profile->buffer_writes += 1U;
        profile->buffer_upload_bytes += static_cast<std::uint64_t>(bytes);
    }
}

// One `sample_terrain_mesh`-style call plus the triangles it tested.
inline void record_terrain_sample(frame_profile* profile, const int triangles_tested) {
    if (profile != nullptr) {
        profile->terrain_sample_calls += 1U;
        if (triangles_tested > 0) {
            profile->terrain_triangles_tested += static_cast<std::uint64_t>(triangles_tested);
        }
    }
}

inline gpu_stage_group child_stage_group(const gpu_profile_stage parent, const gpu_stage_group group) {
    if (parent == gpu_profile_stage::zoom) { return gpu_stage_group::zoom; }
    if (parent == gpu_profile_stage::reflection) {
        return group == gpu_stage_group::zoom ? gpu_stage_group::zoom_reflection : gpu_stage_group::reflection;
    }
    return group;
}

inline void record_timestamp_segment(frame_profile& frame, const gpu_profile_stage stage, const gpu_stage_group group,
                                     const std::uint64_t start, const std::uint64_t end) {
    if (end >= start) {
        frame.gpu_ms[static_cast<std::size_t>(group)][static_cast<std::size_t>(stage)] +=
            static_cast<double>(end - start) / 1000000.0;
    }
}

// Moves everything recorded since `mark` into the debug-overlay buckets so the
// overlay never inflates the counters it is displaying.
struct debug_overlay_cost_mark {
    std::uint32_t draw_calls = 0;
    std::uint32_t uniform_sets = 0;
    std::uint32_t buffer_reallocations = 0;
    std::uint32_t buffer_writes = 0;
    std::uint64_t buffer_upload_bytes = 0;
    std::uint64_t dynamic_upload_bytes = 0;
};

inline debug_overlay_cost_mark mark_debug_overlay_cost(const frame_profile* profile) {
    debug_overlay_cost_mark mark;
    if (profile != nullptr) {
        mark.draw_calls = profile->draw_calls;
        mark.uniform_sets = profile->uniform_sets;
        mark.buffer_reallocations = profile->buffer_reallocations;
        mark.buffer_writes = profile->buffer_writes;
        mark.buffer_upload_bytes = profile->buffer_upload_bytes;
        mark.dynamic_upload_bytes = profile->work.dynamic_upload_bytes;
    }
    return mark;
}

inline void reclaim_debug_overlay_cost(frame_profile* profile, const debug_overlay_cost_mark mark) {
    if (profile == nullptr) {
        return;
    }
    const std::uint32_t draws = profile->draw_calls - mark.draw_calls;
    const std::uint32_t uniforms = profile->uniform_sets - mark.uniform_sets;
    const std::uint32_t buffer_calls = (profile->buffer_writes - mark.buffer_writes) +
                                       (profile->buffer_reallocations - mark.buffer_reallocations);
    const std::uint64_t buffer_bytes = profile->buffer_upload_bytes - mark.buffer_upload_bytes;
    profile->draw_calls = mark.draw_calls;
    profile->uniform_sets = mark.uniform_sets;
    profile->buffer_writes = mark.buffer_writes;
    profile->buffer_reallocations = mark.buffer_reallocations;
    profile->buffer_upload_bytes = mark.buffer_upload_bytes;
    profile->work.dynamic_upload_bytes = mark.dynamic_upload_bytes;
    profile->debug_overlay_draw_calls += draws;
    profile->debug_overlay_uniform_sets += uniforms;
    profile->debug_overlay_buffer_calls += buffer_calls;
    profile->debug_overlay_buffer_bytes += buffer_bytes;
}

class profile_pass_scope {
public:
    profile_pass_scope(frame_profile* frame, const profile_pass pass) : frame_(frame) {
        if (frame_) {
            previous_ = frame_->work.active_pass;
            frame_->work.active_pass = pass;
        }
    }
    ~profile_pass_scope() { if (frame_) { frame_->work.active_pass = previous_; } }
    profile_pass_scope(const profile_pass_scope&) = delete;
    profile_pass_scope& operator=(const profile_pass_scope&) = delete;
private:
    frame_profile* frame_;
    profile_pass previous_ = profile_pass::main;
};

inline double elapsed_milliseconds(const std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// RAII CPU timer. Accumulates into `stage`, so calling it twice in one frame
// reports the total. No-op (and no clock read) when `profile` is null.
class profile_scope {
public:
    profile_scope(frame_profile* profile, const profile_stage stage)
        : profile_(profile), stage_(stage) {
        if (profile_ != nullptr) {
            start_ = std::chrono::steady_clock::now();
        }
    }

    ~profile_scope() {
        if (profile_ == nullptr) {
            return;
        }
        profile_->stage_ms[static_cast<std::size_t>(stage_)] += elapsed_milliseconds(start_);
    }

    profile_scope(const profile_scope&) = delete;
    profile_scope& operator=(const profile_scope&) = delete;
    profile_scope(profile_scope&&) = delete;
    profile_scope& operator=(profile_scope&&) = delete;

private:
    frame_profile* profile_ = nullptr;
    profile_stage stage_ = profile_stage::update_game;
    std::chrono::steady_clock::time_point start_;
};

const char* profile_stage_label(profile_stage stage);
const char* gpu_profile_stage_label(gpu_profile_stage stage);

// Rounded, glyph-safe byte count for the overlay: "512B", "12KB", "34MB".
std::string format_byte_count(std::uint64_t bytes);

// Compact uppercase lines for the bitmap-font debug overlay; timings are
// whole microseconds to keep the lines short.
std::vector<std::string> format_profile_overlay_lines(const frame_profile& profile);
