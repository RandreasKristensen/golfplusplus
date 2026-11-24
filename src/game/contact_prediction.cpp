#include "game/contact_prediction.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr std::int64_t micros_per_milli = 1000;
}

void note_path_sample(my_path_log& log, const snapshot_kind kind, const mover_sample& sample, const std::int64_t keep_from_ms) {
    if (kind != log.kind) {
        log.samples.clear();
        log.kind = kind;
    }
    if (log.samples.empty() || log.samples.back().position != sample.position || log.samples.back().yaw != sample.yaw) {
        mover_sample added = sample;
        if (!log.samples.empty()) {
            added.time_ms = std::max(added.time_ms, log.samples.back().time_ms);
        }
        log.samples.push_back(added);
    }
    std::size_t drop = 0;
    while (drop + 1 < log.samples.size() && log.samples[drop + 1].time_ms <= keep_from_ms) {
        ++drop;
    }
    log.samples.erase(log.samples.begin(), log.samples.begin() + static_cast<std::ptrdiff_t>(drop));
}

mover_path logged_path(const my_path_log& log, const snapshot_entry& mover, const std::int64_t now_ms) {
    mover_path path;
    path.mover = mover;
    path.mover.kind = log.kind;
    path.samples = log.samples;
    path.until_ms = now_ms + 1;
    return path;
}

bool trajectory_near(const shot_result& result, const glm::vec3& position, const float reach) {
    for (const glm::vec3& point : result.trajectory) {
        if (std::abs(point.x - position.x) <= reach && std::abs(point.z - position.z) <= reach) {
            return true;
        }
    }
    return false;
}

bool flight_near(const ball_flight& flight, const glm::vec3& position, const float reach, const std::int64_t from_ms) {
    for (std::size_t chunk = 0; chunk < flight.boxes.size(); ++chunk) {
        const int end_step = static_cast<int>(chunk + 1) * flight_chunk_steps;
        if (step_clock_us(flight.launch_ms, end_step) < from_ms * micros_per_milli) {
            continue;
        }
        const flight_box& box = flight.boxes[chunk];
        const glm::vec3 margin(reach);
        const glm::vec3 low = box.low - margin;
        const glm::vec3 high = box.high + margin;
        if (position.x >= low.x && position.x <= high.x && position.y >= low.y && position.y <= high.y && position.z >= low.z &&
            position.z <= high.z) {
            return true;
        }
    }
    return false;
}

bool prediction_overdue(const ball_flight& flight, const flight_contact& contact, const std::int64_t now_ms, const net_tuning& tuning) {
    const std::int64_t contact_ms = step_clock_us(flight.launch_ms, contact.step + 1) / micros_per_milli;
    return now_ms >= contact_ms + static_cast<std::int64_t>(std::llround(tuning.contact_prediction_hold_seconds * 1000.0f));
}
