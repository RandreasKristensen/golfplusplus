#pragma once

// Lightweight per-frame profiling for the renderer/simulation hot path.
//
// Design rules (see AGENTS.md):
//   - No global mutable state. `app` owns a single `profiler` and hands out a
//     `frame_profile*` to the code that records into it.
//   - A null `frame_profile*` means "profiling disabled". Every record helper
//     and `profile_scope` is a no-op on null, so call sites stay clean and the
//     disabled cost is one predictable branch (no clock reads, no allocation).
//   - `src/physics/` stays pure: physics never receives a `frame_profile*`.
//     Terrain sampling reports work done as a pure output field on
//     `terrain_sample::triangles_tested`; the caller accumulates it.
//
// Usage
// -----
// Owning it (app):
//     profiler profiler_;                       // member of app
//     profiler_.enabled = show_fps_;            // runtime flag
//     profiler_end_frame(profiler_, raw_dt);    // publish previous frame
//     profiler_begin_frame(profiler_);          // start this frame
//     frame_profile* profile = profiler_frame(profiler_);  // null when off
//
// Timing a scope:
//     {
//         profile_scope timer(profile, profile_stage::make_render_data);
//         data = make_render_data(...);
//     }
//
// Counting GL work:
//     record_draw_call(profile);                 // one glDraw* submission
//     record_uniform_set(profile);               // one glUniform* set
//     record_uniform_location_query(profile);    // one glGetUniformLocation
//     record_buffer_upload(profile, byte_count); // one dynamic glBufferData
//
// Accumulating terrain sampling work at the call site:
//     const terrain_sample sample = sample_terrain_mesh(mesh, position, y);
//     record_terrain_sample(profile, sample.triangles_tested);
//
// Optional GPU timings (see renderer/gl_timer.h):
//     record_gpu_stage(profile, gpu_profile_stage::terrain, elapsed_ms);
//
// Reading results: `profiler::published` holds the average over the last
// publish window and is what the debug overlay renders.

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
    count
};

constexpr std::size_t profile_stage_count = static_cast<std::size_t>(profile_stage::count);

enum class gpu_profile_stage : std::size_t {
    terrain = 0,
    trees,
    overlay,
    crt,
    count
};

constexpr std::size_t gpu_profile_stage_count = static_cast<std::size_t>(gpu_profile_stage::count);

// Plain data. Zero-initialised means "nothing recorded".
struct frame_profile {
    double stage_ms[profile_stage_count] = {};
    double gpu_stage_ms[gpu_profile_stage_count] = {};
    bool gpu_timers_available = false;

    std::uint32_t terrain_sample_calls = 0;
    std::uint64_t terrain_triangles_tested = 0;
    std::uint32_t draw_calls = 0;
    std::uint32_t uniform_sets = 0;
    // Actual glGetUniformLocation calls (uniform location cache misses).
    std::uint32_t uniform_location_queries = 0;
    std::uint32_t buffer_uploads = 0;
    std::uint64_t buffer_upload_bytes = 0;
    // Draw/uniform work spent on the debug overlay itself, kept out of the
    // counters above so the numbers describe the real frame.
    std::uint32_t debug_overlay_draw_calls = 0;
    std::uint32_t debug_overlay_uniform_sets = 0;

    float frame_ms = 0.0f;
};

// Owned by `app`. Never a global, never a singleton.
struct profiler {
    bool enabled = false;
    float publish_interval_seconds = 0.25f;

    frame_profile frame;      // being recorded right now
    frame_profile published;  // averaged over the last publish window
    bool has_published = false;

    frame_profile accumulator;
    int accumulated_frames = 0;
    float accumulated_seconds = 0.0f;
    bool frame_open = false;
};

// Starts recording a frame when `enabled`. Clears the live frame.
void profiler_begin_frame(profiler& profile);

// Folds the live frame into the running average and publishes it every
// `publish_interval_seconds`. Safe to call when no frame is open.
void profiler_end_frame(profiler& profile, float frame_seconds);

// Null when profiling is off — pass this straight into the recording helpers.
inline frame_profile* profiler_frame(profiler& profile) {
    return profile.frame_open ? &profile.frame : nullptr;
}

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

inline void record_buffer_upload(frame_profile* profile, const std::size_t bytes) {
    if (profile != nullptr) {
        profile->buffer_uploads += 1U;
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

inline void record_gpu_stage(frame_profile* profile, const gpu_profile_stage stage, const double milliseconds) {
    if (profile != nullptr && stage != gpu_profile_stage::count) {
        profile->gpu_stage_ms[static_cast<std::size_t>(stage)] += milliseconds;
        profile->gpu_timers_available = true;
    }
}

// Moves everything recorded since `mark` into the debug-overlay buckets so the
// overlay never inflates the counters it is displaying.
struct debug_overlay_cost_mark {
    std::uint32_t draw_calls = 0;
    std::uint32_t uniform_sets = 0;
};

inline debug_overlay_cost_mark mark_debug_overlay_cost(const frame_profile* profile) {
    debug_overlay_cost_mark mark;
    if (profile != nullptr) {
        mark.draw_calls = profile->draw_calls;
        mark.uniform_sets = profile->uniform_sets;
    }
    return mark;
}

inline void reclaim_debug_overlay_cost(frame_profile* profile, const debug_overlay_cost_mark mark) {
    if (profile == nullptr) {
        return;
    }
    const std::uint32_t draws = profile->draw_calls - mark.draw_calls;
    const std::uint32_t uniforms = profile->uniform_sets - mark.uniform_sets;
    profile->draw_calls = mark.draw_calls;
    profile->uniform_sets = mark.uniform_sets;
    profile->debug_overlay_draw_calls += draws;
    profile->debug_overlay_uniform_sets += uniforms;
}

// RAII CPU timer. Accumulates into `stage`, so calling it twice in one frame
// reports the total. No-op (and no clock read) when `profile` is null.
struct profile_scope {
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
        const std::chrono::duration<double, std::milli> elapsed =
            std::chrono::steady_clock::now() - start_;
        profile_->stage_ms[static_cast<std::size_t>(stage_)] += elapsed.count();
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

// Compact uppercase lines for the bitmap-font debug overlay. The glyph table in
// the renderer has no '.' so timings are printed as whole microseconds.
std::vector<std::string> format_profile_overlay_lines(const frame_profile& profile);
