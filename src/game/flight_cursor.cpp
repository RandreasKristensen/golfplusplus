#include "game/flight_cursor.h"

#include "physics/ball_physics.h"
#include "physics/cup.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace {
// How far beyond its path a chunk's box reaches, in ball radii.
constexpr float box_margin_radii = 2.0f;

void remember_entry(std::vector<cup_entry>& came_in, const std::size_t cup, const glm::vec3& position) {
    for (cup_entry& entry : came_in) {
        if (entry.cup == cup) {
            entry.position = position;
            return;
        }
    }
    came_in.push_back(cup_entry{cup, position});
}

std::optional<glm::vec3> entry_of(const std::vector<cup_entry>& came_in, const std::size_t cup) {
    for (const cup_entry& entry : came_in) {
        if (entry.cup == cup) {
            return entry.position;
        }
    }
    return std::nullopt;
}

void grow(flight_box& box, const glm::vec3& point) {
    box.low = glm::min(box.low, point);
    box.high = glm::max(box.high, point);
}

// What a step reaches: where it began, where it ended and where its
// velocity alone would have taken it (a bounce ends elsewhere).
void grow_by_step(std::optional<flight_box>& box, const flight_cursor& before, const flight_cursor& after) {
    if (!box) {
        box = flight_box{before.ball.position, before.ball.position};
    }
    grow(*box, after.ball.position);
    grow(*box, before.ball.position + before.ball.velocity * shot_step_seconds);
}
}

solid contact_solid(const flight_contact& contact, const game_tuning& tuning) {
    return snapshot_solids({contact.mover}, tuning).front();
}

flight_cursor start_flight(const ball_state& launched, const shot_course& course) {
    flight_cursor cursor;
    cursor.ball = launched;
    cursor.ground = sample_area(course.area, launched.position);
    cursor.from_green = cursor.ground.material == terrain_material::green;
    return cursor;
}

flight_advance advance_flight(const flight_cursor& cursor, const shot_course& course, const game_tuning& tuning) {
    flight_advance out;
    out.after = cursor;
    flight_cursor& next = out.after;
    const ball_state before = cursor.ball;
    const shot_step step = step_shot(cursor.ball, cursor.ground, course, tuning, shot_step_seconds, cursor.steps);
    ++next.steps;
    next.ball = step.ball;
    next.ground = step.ground;
    out.hit_dynamic = step.hit_dynamic;

    const float time = static_cast<float>(next.steps) * shot_step_seconds;
    const auto add_event = [&out](const shot_event& event) { out.events[static_cast<std::size_t>(out.event_count++)] = event; };
    if (step.landed) {
        add_event(shot_event{time, shot_event_kind::land, next.ground.material});
    }
    if (step.hit && step.hit != cursor.touching) {
        add_event(shot_event{time, *step.hit});
    }
    if (cursor.from_green && step.hit == shot_event_kind::ball_hit && step.hit_dynamic) {
        const solid& other = course.dynamic[*step.hit_dynamic];
        next.hit_ball_on_green =
            next.hit_ball_on_green || sample_area(course.area, other.base).material == terrain_material::green;
    }
    const bool on_person = step.hit == shot_event_kind::person_hit && step.hit_dynamic;
    if (on_person && step.hit_dynamic != cursor.touching_person) {
        const solid& person = course.dynamic[*step.hit_dynamic];
        const float unit = tuning.scale.meters_per_world_unit;
        out.hit = person_hit{*step.hit_dynamic, time, glm::length(before.velocity) * unit,
                             std::clamp(step.ball.position.y - person.base.y, 0.0f, person.height) * unit};
    }
    next.touching_person = on_person ? step.hit_dynamic : std::nullopt;
    next.touching = step.hit;
    if (step.through_leaves && !cursor.in_leaves) {
        add_event(shot_event{time, shot_event_kind::leaves});
    }
    next.in_leaves = step.through_leaves;
    if (step.cup && step.cup != cursor.touching_cup) {
        add_event(shot_event{time, shot_event_kind::cup_rattle});
        remember_entry(next.came_in, *step.cup, before.position);
    }
    next.touching_cup = step.cup;
    if (ball_in_water(next.ball, next.ground) && next.ball.position.y + next.ball.radius < next.ground.water_level) {
        next.lost = true;
    }
    if (next.lost) {
        const int linger_steps = static_cast<int>(std::ceil(std::max(0.0f, tuning.ball.water_linger_seconds) / shot_step_seconds));
        next.on_pond_floor = next.on_pond_floor >= 0 || ball_is_grounded(next.ball, next.ground) ? next.on_pond_floor + 1 : -1;
        next.finished = next.on_pond_floor >= linger_steps;
        return out;
    }
    if (step.plugged) {
        next.finished = true;
    } else if (step.resting && glm::length(next.ball.velocity) <= tuning.ball.stop_speed) {
        if (step.cup && in_cup(next.ball, course.area.cups[*step.cup])) {
            next.resting_cup = step.cup;
        }
        next.finished = true;
    }
    return out;
}

flight_stepper::flight_stepper(const shot_course& course, const game_tuning& tuning,
                               const std::vector<flight_contact>& contacts)
    : course_(course), tuning_(tuning), contacts_(contacts) {}

flight_advance flight_stepper::advance(const flight_cursor& cursor) {
    std::optional<std::size_t> index = cursor.in_contact;
    for (std::size_t i = 0; i < contacts_.size(); ++i) {
        if (contacts_[i].step == cursor.steps) {
            index = i;
        }
    }
    if (!index) {
        return advance_flight(cursor, course_, tuning_);
    }
    if (built_for_ != index) {
        with_mover_.emplace(course_);
        with_mover_->dynamic.push_back(contact_solid(contacts_[*index], tuning_));
        with_mover_->shooter = contacts_[*index].mover.shooter ? std::optional<std::size_t>(with_mover_->dynamic.size() - 1)
                                                               : std::nullopt;
        built_for_ = index;
    }
    flight_advance out = advance_flight(cursor, *with_mover_, tuning_);
    const bool met = out.hit_dynamic && *out.hit_dynamic == course_.dynamic.size();
    out.after.in_contact = met ? index : std::nullopt;
    if (met && out.hit) {
        out.hit->entry = *index;
    }
    return out;
}

flight_run start_run(const ball_state& launched, const shot_course& course, const bool recording) {
    flight_run run;
    run.cursor = start_flight(launched, course);
    run.launch_position = launched.position;
    run.recording = recording;
    run.result.trajectory.push_back(launched.position);
    return run;
}

flight_run continue_run(flight_run run, const shot_course& course, const game_tuning& tuning,
                        const std::vector<flight_contact>& contacts) {
    flight_stepper stepper(course, tuning, contacts);
    while (!run.cursor.finished && run.cursor.steps < shot_max_steps) {
        if (run.recording && run.cursor.steps % flight_checkpoint_steps == 0) {
            run.checkpoints.push_back(flight_checkpoint{run.cursor, static_cast<std::uint32_t>(run.result.events.size()),
                                                        static_cast<std::uint32_t>(run.result.person_hits.size())});
        }
        flight_advance advance = stepper.advance(run.cursor);
        if (run.recording) {
            grow_by_step(run.open_box, run.cursor, advance.after);
        }
        for (int i = 0; i < advance.event_count; ++i) {
            run.result.events.push_back(advance.events[static_cast<std::size_t>(i)]);
        }
        if (advance.hit) {
            run.result.person_hits.push_back(*advance.hit);
        }
        run.cursor = std::move(advance.after);
        if (run.cursor.in_contact) {
            run.contacts_end_step = run.cursor.steps;
        }
        if (run.cursor.steps % shot_steps_per_trajectory_point == 0) {
            run.result.trajectory.push_back(run.cursor.ball.position);
        }
        if (run.recording && run.cursor.steps % flight_chunk_steps == 0) {
            const glm::vec3 margin(box_margin_radii * run.cursor.ball.radius);
            run.boxes.push_back(flight_box{run.open_box->low - margin, run.open_box->high + margin});
            run.open_box.reset();
        }
    }
    if (run.open_box) {
        const glm::vec3 margin(box_margin_radii * run.cursor.ball.radius);
        run.boxes.push_back(flight_box{run.open_box->low - margin, run.open_box->high + margin});
        run.open_box.reset();
    }
    return run;
}

shot_result finish_run(const flight_run& run, const shot_course& course, const game_tuning& tuning) {
    shot_result result = run.result;
    const flight_cursor& end = run.cursor;
    if (end.steps % shot_steps_per_trajectory_point != 0) {
        result.trajectory.push_back(end.ball.position);
    }
    result.lost = end.lost;
    result.duration = static_cast<float>(end.steps) * shot_step_seconds;
    result.holed = end.resting_cup && (course.practice || *end.resting_cup == course.hole.index);
    result.rest_position = end.lost ? run.launch_position : end.ball.position;
    result.penalty_strokes = (end.lost ? 1 : 0) + (end.hit_ball_on_green ? 2 : 0);
    if (end.resting_cup && !result.holed) {
        // Lifted out of another green's cup and dropped beside it.
        const cup& other = course.area.cups[*end.resting_cup];
        const glm::vec3 came = entry_of(end.came_in, *end.resting_cup).value_or(glm::vec3(0.0f));
        const glm::vec3 side = safe_normalize(horizontal(came - other.centre), glm::vec3(1.0f, 0.0f, 0.0f));
        const glm::vec3 drop = other.centre + side * (tuning.cup.wrong_cup_drop_distance_meters / tuning.scale.meters_per_world_unit);
        result.rest_position = resting_ball_position(course.area, drop, end.ball.radius);
    }
    return result;
}

shot_result simulate_ball(const ball_state& launched, const shot_course& course, const game_tuning& tuning) {
    const std::vector<flight_contact> no_contacts;
    return finish_run(continue_run(start_run(launched, course, false), course, tuning, no_contacts), course, tuning);
}
