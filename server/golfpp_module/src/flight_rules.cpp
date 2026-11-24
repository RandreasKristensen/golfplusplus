#include "flight_rules.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>

namespace {
std::int64_t milliseconds(const float seconds) {
    return std::llround(static_cast<double>(seconds) * 1000.0);
}

// `movers` standing, each searched against `live`'s flight: the earliest
// contact (the first found when two share a step).
std::vector<flight_contact> standing_contacts(const live_flight& live, const std::vector<known_mover>& movers,
                                              const shot_course& flown, const game_tuning& tuning) {
    std::vector<flight_contact> pending;
    const std::int64_t until = flight_end_ms(live.flight) + 1;
    for (const known_mover& mover : movers) {
        const std::int64_t from = std::min(mover.since_ms, until - 1);
        if (const std::optional<flight_contact> found =
                first_contact(live.flight, standing_path(mover.entry, from, until), flown, tuning)) {
            pending = pending_with(pending, *found);
        }
    }
    return pending;
}

bool was_rolled(const live_flight& live, const std::uint64_t account) {
    return std::find(live.rolled.begin(), live.rolled.end(), account) != live.rolled.end();
}
}

bool same_mover(const snapshot_entry& a, const snapshot_entry& b) {
    return a.kind == b.kind && a.account == b.account;
}

std::int64_t flight_end_ms(const ball_flight& flight) {
    return (step_clock_us(flight.launch_ms, flight.steps) + 999) / 1000;
}

live_flight begin_flight(const shot_record& record, const golfer_profile& golfer, const int hole, const int stroke,
                         const std::vector<int>& flags_out, const std::vector<known_mover>& movers, const flight_env& env) {
    live_flight live;
    live.record = record;
    live.golfer = golfer;
    live.hole = hole;
    live.stroke = stroke;
    live.flags_out = flags_out;
    live.flight = fly_struck_shot(record, golfer, env.course, env.tuning, env.clubs);
    live.pending = standing_contacts(live, movers, struck_course(record, env.course, env.tuning), env.tuning);
    return live;
}

live_flight flight_after_movement(live_flight live, mover_path path, const bool gone, const std::vector<known_mover>& others,
                                  const flight_env& env) {
    if (path.samples.empty()) {
        return live;
    }
    const shot_course flown = struck_course(live.record, env.course, env.tuning);
    path.until_ms = gone ? path.samples.back().time_ms + 1 : flight_end_ms(live.flight) + 1;
    const std::optional<flight_contact> found = first_contact(live.flight, path, flown, env.tuning);
    const bool superseded = !live.pending.empty() && same_mover(live.pending.front().mover, path.mover) &&
        step_clock_us(live.flight.launch_ms, live.pending.front().step + 1) >= path.samples.front().time_ms * 1000;
    if (superseded) {
        live.pending = standing_contacts(live, others, flown, env.tuning);
    }
    if (found) {
        live.pending = pending_with(live.pending, *found);
    }
    return live;
}

settlement settle_flight(live_flight live, const std::int64_t settled_until, const std::vector<known_mover>& movers,
                         const flight_env& env) {
    settlement settled;
    const shot_course flown = struck_course(live.record, env.course, env.tuning);
    const float person_height = env.tuning.rangefinder.person_height * env.tuning.scale.meters_per_world_unit;
    while (!live.pending.empty() && contact_settled(live.flight, live.pending.front(), settled_until)) {
        const flight_contact contact = live.pending.front();
        live.pending.clear();
        const std::size_t before = live.flight.contacts.size();
        live.flight = replay_after_contact(live.flight, contact, flown, env.tuning);
        if (live.flight.contacts.size() == before) {
            break;
        }
        applied_contact applied{contact, static_cast<int>(before), hit_outcome_kind::none};
        if (contact.mover.kind == snapshot_kind::person && !was_rolled(live, contact.mover.account)) {
            live.rolled.push_back(contact.mover.account);
            applied.outcome = hit_outcome(contact.speed_mps, contact.height_m, person_height, contact.mover.shooter,
                                          env.tuning.ball_hits, live.record.seed, applied.index);
        }
        settled.applied.push_back(applied);
        std::vector<known_mover> standing;
        for (const known_mover& mover : movers) {
            standing.push_back(same_mover(mover.entry, contact.mover) ? known_mover{contact.mover, milliseconds(contact.time) + live.flight.launch_ms}
                                                                       : mover);
        }
        live.pending = standing_contacts(live, standing, flown, env.tuning);
    }
    settled.live = std::move(live);
    return settled;
}

std::int64_t landing_due_ms(const live_flight& live, const server_tuning& tuning) {
    return flight_end_ms(live.flight) + milliseconds(tuning.contact_rewind_seconds);
}

std::int64_t next_due_ms(const live_flight& live, const server_tuning& tuning) {
    std::int64_t due = landing_due_ms(live, tuning);
    if (!live.pending.empty()) {
        due = std::min(due, settle_due_ms(live.flight, live.pending.front(), tuning));
    }
    return due;
}

std::vector<float> flight_box_values(const ball_flight& flight) {
    std::vector<float> values;
    values.reserve(flight.boxes.size() * 6);
    for (const flight_box& box : flight.boxes) {
        values.insert(values.end(), {box.low.x, box.low.y, box.low.z, box.high.x, box.high.y, box.high.z});
    }
    return values;
}

std::vector<std::int64_t> stepped_times_near(const std::vector<float>& boxes, const std::vector<known_mover>& movers,
                                             const float reach) {
    glm::vec3 low(0.0f);
    glm::vec3 high(0.0f);
    for (std::size_t i = 0; i * 6 + 5 < boxes.size(); ++i) {
        const glm::vec3 a(boxes[i * 6], boxes[i * 6 + 1], boxes[i * 6 + 2]);
        const glm::vec3 b(boxes[i * 6 + 3], boxes[i * 6 + 4], boxes[i * 6 + 5]);
        low = i == 0 ? a : glm::min(low, a);
        high = i == 0 ? b : glm::max(high, b);
    }
    std::vector<std::int64_t> times;
    for (const known_mover& mover : movers) {
        const glm::vec3& at = mover.entry.position;
        if (mover.since_ms != 0 && at.x >= low.x - reach && at.x <= high.x + reach && at.z >= low.z - reach && at.z <= high.z + reach) {
            times.push_back(mover.since_ms);
        }
    }
    return times;
}

bool path_near_flight(const std::vector<float>& boxes, const std::int64_t launch_ms, const mover_path& path,
                      const game_tuning& tuning) {
    for (std::size_t chunk = 0; chunk * 6 + 5 < boxes.size(); ++chunk) {
        const int start = static_cast<int>(chunk) * flight_chunk_steps;
        const std::optional<world_box> mover = swept_box(path, step_clock_us(launch_ms, start),
                                                         step_clock_us(launch_ms, start + flight_chunk_steps), tuning);
        const float* b = &boxes[chunk * 6];
        if (mover && boxes_overlap(*mover, world_box{glm::vec3(b[0], b[1], b[2]), glm::vec3(b[3], b[4], b[5])})) {
            return true;
        }
    }
    return false;
}
