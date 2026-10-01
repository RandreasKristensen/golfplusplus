#include "renderer/gl_timer.h"

#include "renderer/gl_loader.h"

void gl_timer_pool::init() {
    shutdown();
    for (int slot = 0; slot < slot_count; ++slot) {
        glGenQueries(static_cast<GLsizei>(gpu_profile_stage_count), queries_[slot]);
    }
    initialized_ = true;
}

void gl_timer_pool::shutdown() {
    if (initialized_) {
        for (int slot = 0; slot < slot_count; ++slot) {
            glDeleteQueries(static_cast<GLsizei>(gpu_profile_stage_count), queries_[slot]);
        }
    }
    *this = gl_timer_pool{};
}

void gl_timer_pool::begin_frame(const bool record) {
    if (!initialized_) {
        return;
    }

    end();
    recording_ = record;
    if (!record) {
        frames_written_ = 0;
        for (auto& slot : issued_) {
            for (bool& issued : slot) {
                issued = false;
            }
        }
        return;
    }
    write_slot_ = (write_slot_ + 1) % slot_count;
    for (bool& issued : issued_[write_slot_]) {
        issued = false;
    }
    if (frames_written_ < slot_count) {
        ++frames_written_;
    }
}

void gl_timer_pool::begin(const gpu_profile_stage stage) {
    if (!initialized_ || !recording_ || stage == gpu_profile_stage::count || active_stage_ != gpu_profile_stage::count) {
        return;
    }
    glBeginQuery(GL_TIME_ELAPSED, queries_[write_slot_][static_cast<std::size_t>(stage)]);
    active_stage_ = stage;
}

void gl_timer_pool::end() {
    if (!initialized_ || active_stage_ == gpu_profile_stage::count) {
        return;
    }
    glEndQuery(GL_TIME_ELAPSED);
    issued_[write_slot_][static_cast<std::size_t>(active_stage_)] = true;
    active_stage_ = gpu_profile_stage::count;
}

void gl_timer_pool::collect(frame_profile* profile) {
    if (!initialized_ || !recording_ || profile == nullptr || frames_written_ < slot_count) {
        return;
    }

    const int read_slot = (write_slot_ + 1) % slot_count;
    for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
        if (!issued_[read_slot][stage]) {
            continue;
        }
        GLuint done = 0U;
        glGetQueryObjectuiv(queries_[read_slot][stage], GL_QUERY_RESULT_AVAILABLE, &done);
        if (done == 0U) {
            continue;
        }
        GLuint64 nanoseconds = 0U;
        glGetQueryObjectui64v(queries_[read_slot][stage], GL_QUERY_RESULT, &nanoseconds);
        issued_[read_slot][stage] = false;
        record_gpu_stage(profile, static_cast<gpu_profile_stage>(stage), static_cast<double>(nanoseconds) / 1000000.0);
    }
}
