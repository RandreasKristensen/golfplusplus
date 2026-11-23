#include "renderer/gl_timer.h"
#include "renderer/gl_loader.h"

namespace {
GLuint64 timestamp(const GLuint query) {
    GLuint64 value = 0;
    glGetQueryObjectui64v(query, GL_QUERY_RESULT, &value);
    return value;
}
}

void gl_timer_pool::init() {
    shutdown();
    GLint bits = 0;
    glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
    supported_ = bits > 0;
    if (supported_) {
        for (auto& pending : slots_) { glGenQueries(2, pending.bounds); }
    }
}

void gl_timer_pool::shutdown() {
    if (supported_) {
        for (auto& pending : slots_) {
            glDeleteQueries(2, pending.bounds);
            for (const auto& part : pending.segments) { glDeleteQueries(2, part.query); }
        }
    }
    *this = gl_timer_pool{};
}

bool gl_timer_pool::resolve(slot& pending, frame_profile* cost) {
    const profile_scope timer(cost, profile_stage::query_read);
    GLuint available = 0;
    // Every timestamp has the same query target. Availability of the last covers all earlier pairs.
    glGetQueryObjectuiv(pending.bounds[1], GL_QUERY_RESULT_AVAILABLE, &available);
    if (!available) { return false; }
    const auto start = timestamp(pending.bounds[0]);
    const auto end = timestamp(pending.bounds[1]);
    if (end >= start) {
        pending.frame.gpu_timers_available = true;
        pending.frame.gpu_frame_ms = static_cast<double>(end - start) / 1000000.0;
        for (std::size_t i = 0; i < pending.used; ++i) {
            const auto& part = pending.segments[i];
            record_timestamp_segment(pending.frame, part.stage, part.group, timestamp(part.query[0]), timestamp(part.query[1]));
        }
    }
    return true;
}

void gl_timer_pool::begin_frame(frame_profile* profile) {
    ++sequence_;
    stack_.clear();
    if (!supported_) { return; }
    if (!profile) {
        // Frames issued before the overlay closed must not reach the next average.
        while (ring_.oldest(sequence_, true)) { ring_.release(); }
        completed_.reset();
        return;
    }
    if (const auto oldest = ring_.oldest(sequence_)) {
        auto& pending = slots_[*oldest];
        if (resolve(pending, profile)) {
            completed_ = pending.frame;
            ring_.release();
        }
    }
    writing_ = ring_.reserve(sequence_);
    if (writing_) {
        auto& current = slots_[*writing_];
        current.used = 0;
        glQueryCounter(current.bounds[0], GL_TIMESTAMP);
    }
}

void gl_timer_pool::begin(const gpu_profile_stage stage) {
    if (!writing_) { return; }
    auto& current = slots_[*writing_];
    if (current.used == current.segments.size()) {
        current.segments.emplace_back();
        glGenQueries(2, current.segments.back().query);
    }
    auto& part = current.segments[current.used];
    part.stage = stage;
    part.group = gpu_stage_group::scene;
    if (!stack_.empty()) {
        const auto& parent = current.segments[stack_.back()];
        part.group = child_stage_group(parent.stage, parent.group);
    }
    stack_.push_back(current.used++);
    glQueryCounter(part.query[0], GL_TIMESTAMP);
}

void gl_timer_pool::end() {
    if (!writing_ || stack_.empty()) { return; }
    glQueryCounter(slots_[*writing_].segments[stack_.back()].query[1], GL_TIMESTAMP);
    stack_.pop_back();
}

void gl_timer_pool::end_frame() {
    if (writing_) { glQueryCounter(slots_[*writing_].bounds[1], GL_TIMESTAMP); }
}

void gl_timer_pool::finish(const frame_profile& frame, const frame_profile_sink& sink) {
    if (completed_) {
        sink(*completed_);
        completed_.reset();
    }
    if (writing_) {
        slots_[*writing_].frame = frame;
        writing_.reset();
    } else {
        sink(frame);
    }
}

void gl_timer_pool::flush(const frame_profile_sink& sink) {
    while (const auto oldest = ring_.oldest(sequence_, true)) {
        auto& pending = slots_[*oldest];
        resolve(pending, nullptr);
        sink(pending.frame);
        ring_.release();
    }
}
