#pragma once

#include "profiling/profiling.h"

// Optional GPU timer queries (GL_TIME_ELAPSED, core since OpenGL 3.3).
//
// Entirely optional and self-disabling: if the entry points or query objects
// cannot be obtained, init() returns false, available() stays false and every
// other call is a no-op. No global state — the renderer owns one pool.
//
// Results are read back without stalling: a ring of frame slots is used and a
// frame's timings are collected once the GPU has finished with them (typically
// two frames later), which is fine for a debug overlay.
struct gl_timer_pool {
    static constexpr int slot_count = 3;

    bool init();
    void shutdown();
    bool available() const { return ready_; }

    // Call once at the start of a frame, before any begin()/end() pair.
    // Pass record=false (profiling off) to skip issuing queries entirely.
    void begin_frame(bool record);
    void begin(gpu_profile_stage stage);
    void end();
    // Writes any finished timings into `profile` (safe with a null profile).
    void collect(frame_profile* profile);

private:
    // Loaded through SDL_GL_GetProcAddress into members, never into globals.
    void* gen_queries_ = nullptr;
    void* delete_queries_ = nullptr;
    void* begin_query_ = nullptr;
    void* end_query_ = nullptr;
    void* get_query_object_uiv_ = nullptr;
    void* get_query_object_ui64v_ = nullptr;

    unsigned int queries_[slot_count][gpu_profile_stage_count] = {};
    bool issued_[slot_count][gpu_profile_stage_count] = {};
    int write_slot_ = 0;
    int frames_written_ = 0;
    gpu_profile_stage active_stage_ = gpu_profile_stage::count;
    bool ready_ = false;
    bool recording_ = false;
};
