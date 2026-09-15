#include "renderer/gl_timer.h"

#include <SDL.h>

#include "core/gl_loader.h"
#include "renderer/gl_proc.h"

#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_QUERY_RESULT
#define GL_QUERY_RESULT 0x8866
#endif
#ifndef GL_QUERY_RESULT_AVAILABLE
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#endif

namespace {
using gen_queries_fn = void (APIENTRY*)(GLsizei, GLuint*);
using delete_queries_fn = void (APIENTRY*)(GLsizei, const GLuint*);
using begin_query_fn = void (APIENTRY*)(GLenum, GLuint);
using end_query_fn = void (APIENTRY*)(GLenum);
using get_query_object_uiv_fn = void (APIENTRY*)(GLuint, GLenum, GLuint*);
using get_query_object_ui64v_fn = void (APIENTRY*)(GLuint, GLenum, GLuint64*);
}

bool gl_timer_pool::init() {
    shutdown();

    gen_queries_ = load_gl_proc("glGenQueries");
    delete_queries_ = load_gl_proc("glDeleteQueries");
    begin_query_ = load_gl_proc("glBeginQuery");
    end_query_ = load_gl_proc("glEndQuery");
    get_query_object_uiv_ = load_gl_proc("glGetQueryObjectuiv");
    get_query_object_ui64v_ = load_gl_proc("glGetQueryObjectui64v");

    if (gen_queries_ == nullptr || delete_queries_ == nullptr || begin_query_ == nullptr ||
        end_query_ == nullptr || get_query_object_uiv_ == nullptr || get_query_object_ui64v_ == nullptr) {
        SDL_Log("GPU timer queries unavailable; CPU profiling only.");
        shutdown();
        return false;
    }

    for (int slot = 0; slot < slot_count; ++slot) {
        reinterpret_cast<gen_queries_fn>(gen_queries_)(static_cast<GLsizei>(gpu_profile_stage_count),
                                                       queries_[slot]);
        for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
            if (queries_[slot][stage] == 0U) {
                SDL_Log("GPU timer query allocation failed; CPU profiling only.");
                shutdown();
                return false;
            }
            issued_[slot][stage] = false;
        }
    }

    write_slot_ = 0;
    frames_written_ = 0;
    active_stage_ = gpu_profile_stage::count;
    ready_ = true;
    recording_ = false;
    return true;
}

void gl_timer_pool::shutdown() {
    if (delete_queries_ != nullptr) {
        for (int slot = 0; slot < slot_count; ++slot) {
            if (queries_[slot][0] != 0U) {
                reinterpret_cast<delete_queries_fn>(delete_queries_)(static_cast<GLsizei>(gpu_profile_stage_count),
                                                                     queries_[slot]);
            }
        }
    }

    for (int slot = 0; slot < slot_count; ++slot) {
        for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
            queries_[slot][stage] = 0U;
            issued_[slot][stage] = false;
        }
    }

    gen_queries_ = nullptr;
    delete_queries_ = nullptr;
    begin_query_ = nullptr;
    end_query_ = nullptr;
    get_query_object_uiv_ = nullptr;
    get_query_object_ui64v_ = nullptr;
    write_slot_ = 0;
    frames_written_ = 0;
    active_stage_ = gpu_profile_stage::count;
    ready_ = false;
    recording_ = false;
}

void gl_timer_pool::begin_frame(const bool record) {
    if (!ready_) {
        return;
    }

    end();
    recording_ = record;
    if (!record) {
        frames_written_ = 0;
        for (int slot = 0; slot < slot_count; ++slot) {
            for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
                issued_[slot][stage] = false;
            }
        }
        return;
    }
    write_slot_ = (write_slot_ + 1) % slot_count;
    for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
        issued_[write_slot_][stage] = false;
    }
    if (frames_written_ < slot_count) {
        ++frames_written_;
    }
}

void gl_timer_pool::begin(const gpu_profile_stage stage) {
    if (!ready_ || !recording_ || stage == gpu_profile_stage::count || active_stage_ != gpu_profile_stage::count) {
        return;
    }

    const std::size_t index = static_cast<std::size_t>(stage);
    reinterpret_cast<begin_query_fn>(begin_query_)(GL_TIME_ELAPSED, queries_[write_slot_][index]);
    active_stage_ = stage;
}

void gl_timer_pool::end() {
    if (!ready_ || active_stage_ == gpu_profile_stage::count) {
        return;
    }

    reinterpret_cast<end_query_fn>(end_query_)(GL_TIME_ELAPSED);
    issued_[write_slot_][static_cast<std::size_t>(active_stage_)] = true;
    active_stage_ = gpu_profile_stage::count;
}

void gl_timer_pool::collect(frame_profile* profile) {
    if (!ready_ || !recording_ || profile == nullptr || frames_written_ < slot_count) {
        return;
    }

    const int read_slot = (write_slot_ + 1) % slot_count;
    for (std::size_t stage = 0; stage < gpu_profile_stage_count; ++stage) {
        if (!issued_[read_slot][stage]) {
            continue;
        }

        GLuint done = 0U;
        reinterpret_cast<get_query_object_uiv_fn>(get_query_object_uiv_)(queries_[read_slot][stage],
                                                                         GL_QUERY_RESULT_AVAILABLE,
                                                                         &done);
        if (done == 0U) {
            continue;
        }

        GLuint64 nanoseconds = 0U;
        reinterpret_cast<get_query_object_ui64v_fn>(get_query_object_ui64v_)(queries_[read_slot][stage],
                                                                             GL_QUERY_RESULT,
                                                                             &nanoseconds);
        issued_[read_slot][stage] = false;
        record_gpu_stage(profile, static_cast<gpu_profile_stage>(stage), static_cast<double>(nanoseconds) / 1000000.0);
    }
}
