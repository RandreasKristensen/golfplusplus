#include "game/ball_contact.h"

#include "game/shot_simulation.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// How far past the mover's solid a ball's path may pass and still be
// confirmed by stepping, in ball radii (the same reach as a chunk's box).
constexpr float candidate_reach_radii = 2.0f;

glm::vec3 across_of(const glm::vec3& along) {
    return glm::vec3(along.z, 0.0f, -along.x);
}

// Whether the segment a-b (XZ) comes within `reach` of the axis at `centre`.
bool segment_near_axis(const glm::vec3& a, const glm::vec3& b, const glm::vec3& centre, const float reach) {
    const glm::vec2 from(a.x, a.z);
    const glm::vec2 along = glm::vec2(b.x, b.z) - from;
    const glm::vec2 to_centre = glm::vec2(centre.x, centre.z) - from;
    const float length_squared = glm::dot(along, along);
    const float share = length_squared > 0.0f ? std::clamp(glm::dot(to_centre, along) / length_squared, 0.0f, 1.0f) : 0.0f;
    return glm::length(to_centre - along * share) <= reach;
}

// Whether the segment from (u0, w0) to (u1, w1) meets the rectangle of half
// extents (hu, hw) about the origin (a slab test).
bool segment_in_rect(const float u0, const float w0, const float u1, const float w1, const float hu, const float hw) {
    float enter = 0.0f;
    float leave = 1.0f;
    const auto clip = [&enter, &leave](const float from, const float delta, const float half) {
        if (std::abs(delta) < 1e-12f) {
            return std::abs(from) <= half;
        }
        const float a = (-half - from) / delta;
        const float b = (half - from) / delta;
        enter = std::max(enter, std::min(a, b));
        leave = std::min(leave, std::max(a, b));
        return true;
    };
    return clip(u0, u1 - u0, hu) && clip(w0, w1 - w0, hw) && enter <= leave;
}

// Whether the ball over a step (from where it began to where it ended, and
// to where its velocity alone would take it: a bounce ends elsewhere) may
// touch `thing`: a conservative test, confirmed by stepping.
bool may_touch(const flight_cursor& before, const flight_cursor& after, const solid& thing) {
    const float reach = candidate_reach_radii * before.ball.radius;
    const glm::vec3& start = before.ball.position;
    const glm::vec3 ends[2] = {after.ball.position, start + before.ball.velocity * shot_step_seconds};
    for (const glm::vec3& end : ends) {
        if (std::min(start.y, end.y) - reach > thing.base.y + thing.height || std::max(start.y, end.y) + reach < thing.base.y) {
            continue;
        }
        if (thing.shape == solid_shape::box) {
            const glm::vec3 across = across_of(thing.along);
            const glm::vec3 from = start - thing.base;
            const glm::vec3 to = end - thing.base;
            if (segment_in_rect(glm::dot(from, thing.along), glm::dot(from, across), glm::dot(to, thing.along),
                                glm::dot(to, across), thing.half_length + reach, thing.half_width + reach)) {
                return true;
            }
        } else if (segment_near_axis(start, end, thing.base, thing.radius + reach)) {
            return true;
        }
    }
    return false;
}

// `course` with `thing` as its last dynamic solid, the shooter's own when `shooter`.
shot_course with_mover(const shot_course& course, const solid& thing, const bool shooter) {
    shot_course met = course;
    met.dynamic.push_back(thing);
    met.shooter = shooter ? std::optional<std::size_t>(met.dynamic.size() - 1) : std::nullopt;
    return met;
}
}

std::optional<flight_contact> first_contact(const ball_flight& flight, const mover_path& path, const shot_course& flown_course,
                                            const game_tuning& tuning, contact_search_stats* stats) {
    contact_search_stats local;
    contact_search_stats& counted = stats != nullptr ? *stats : local;
    const float unit = tuning.scale.meters_per_world_unit;
    const std::size_t first_chunk = static_cast<std::size_t>(flight.open_from_step / flight_chunk_steps);
    for (std::size_t chunk = first_chunk; chunk < flight.boxes.size(); ++chunk) {
        ++counted.chunks_tested;
        const int chunk_start = static_cast<int>(chunk) * flight_chunk_steps;
        const std::optional<world_box> mover = swept_box(path, step_clock_us(flight.launch_ms, chunk_start),
                                                         step_clock_us(flight.launch_ms, chunk_start + flight_chunk_steps), tuning);
        const world_box ball{flight.boxes[chunk].low, flight.boxes[chunk].high};
        if (!mover || !boxes_overlap(*mover, ball)) {
            continue;
        }
        ++counted.chunks_overlapping;
        flight_cursor cursor = flight.checkpoints[static_cast<std::size_t>(chunk_start / flight_checkpoint_steps)].cursor;
        flight_stepper stepper(flown_course, tuning, flight.contacts);
        const int chunk_end = std::min(chunk_start + flight_chunk_steps, flight.steps);
        for (int step = chunk_start; step < chunk_end; ++step) {
            const flight_advance flown = stepper.advance(cursor);
            const std::optional<snapshot_entry> there = mover_at(path, step_clock_us(flight.launch_ms, step + 1));
            if (step >= flight.open_from_step && there) {
                const solid thing = snapshot_solid(*there, tuning);
                ++counted.steps_tested;
                if (may_touch(cursor, flown.after, thing)) {
                    ++counted.steps_confirmed;
                    const shot_course met = with_mover(flown_course, thing, there->shooter);
                    const flight_advance struck = advance_flight(cursor, met, tuning);
                    if (struck.hit_dynamic && *struck.hit_dynamic == flown_course.dynamic.size()) {
                        flight_contact contact;
                        contact.step = step;
                        contact.mover = *there;
                        contact.time = static_cast<float>(step + 1) * shot_step_seconds;
                        contact.speed_mps = glm::length(cursor.ball.velocity) * unit;
                        contact.height_m = std::clamp(struck.after.ball.position.y - thing.base.y, 0.0f, thing.height) * unit;
                        return contact;
                    }
                }
            }
            cursor = flown.after;
        }
    }
    return std::nullopt;
}

ball_flight replay_after_contact(const ball_flight& flight, const flight_contact& contact, const shot_course& flown_course,
                                 const game_tuning& tuning) {
    if (contact.step < flight.open_from_step || contact.step >= flight.steps) {
        return flight;
    }
    std::vector<flight_contact> contacts = flight.contacts;
    contacts.push_back(contact);
    return resume_flight(flight, contact.step, contacts, flown_course, tuning);
}

ball_flight fly_struck_shot(const shot_record& record, const golfer_profile& golfer, const shot_course& course,
                            const game_tuning& tuning, const std::vector<club_definition>& clubs) {
    return replay_contacts(record, golfer, course, tuning, clubs, {});
}

ball_flight replay_contacts(const shot_record& record, const golfer_profile& golfer, const shot_course& course,
                            const game_tuning& tuning, const std::vector<club_definition>& clubs,
                            const std::vector<flight_contact>& contacts) {
    const shot_course flown = struck_course(record, course, tuning);
    const std::optional<shot_launch> launch =
        launch_shot(record.setup, flown, tuning, clubs, struck_swing_errors(record, golfer, course, tuning, clubs));
    if (!launch) {
        ball_flight still;
        still.launch_ms = record.setup.shot_at;
        still.result.rest_position = record.setup.ball_start;
        still.result.trajectory.push_back(record.setup.ball_start);
        return still;
    }
    ball_flight flight = fly_ball(launch->ball, record.setup.shot_at, flown, tuning, contacts);
    flight.result.strike_centredness = launch->strike_centredness;
    return flight;
}
