#pragma once

// Delayed GL_TIMESTAMP pairs for complete frames and every invocation of a stage.
#include "profiling/profiling.h"
#include "profiling/timestamp_ring.h"
#include <array>
#include <optional>
#include <vector>

class gl_timer_pool {
public:
    void init();
    void shutdown();
    void begin_frame(frame_profile* profile);
    void end_frame();
    void begin(gpu_profile_stage stage);
    void end();
    // Attach post-swap CPU data to the issuing slot and deliver resolved records.
    void finish(const frame_profile& frame, const frame_profile_sink& sink);
    // Deliver remaining records at shutdown. GPU fields stay unavailable if still busy.
    void flush(const frame_profile_sink& sink);

private:
    struct segment {
        unsigned int query[2]{};
        gpu_profile_stage stage{};
        gpu_stage_group group{};
    };
    struct slot {
        unsigned int bounds[2]{};
        std::vector<segment> segments;
        std::size_t used = 0;
        frame_profile frame;
    };
    bool resolve(slot& pending, frame_profile* cost);
    timestamp_ring ring_;
    std::array<slot, timestamp_ring::capacity> slots_;
    std::optional<std::size_t> writing_;
    std::optional<frame_profile> completed_;
    std::vector<std::size_t> stack_;
    std::uint64_t sequence_ = 0;
    bool supported_ = false;
};
