#pragma once

#include "profiling/profiling.h"

// GPU timer queries (GL_TIME_ELAPSED) for the profiling overlay.
//
// Results are read back without stalling: a ring of frame slots is used and a
// frame's timings are collected once the GPU has finished with them (usually
// two frames later), which is fine for a debug overlay.
class gl_timer_pool {
public:
    static constexpr int slot_count = 3;

    void init();
    void shutdown();

    // Call once at the start of a frame. record=false (profiling off) issues
    // no queries.
    void begin_frame(bool record);
    void begin(gpu_profile_stage stage);
    void end();
    // Writes finished timings into `profile` (safe with null).
    void collect(frame_profile* profile);

private:
    unsigned int queries_[slot_count][gpu_profile_stage_count] = {};
    bool issued_[slot_count][gpu_profile_stage_count] = {};
    int write_slot_ = 0;
    int frames_written_ = 0;
    gpu_profile_stage active_stage_ = gpu_profile_stage::count;
    bool initialized_ = false;
    bool recording_ = false;
};
