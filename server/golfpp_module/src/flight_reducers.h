#pragma once
// Shots in flight, SDK side, included by the single reducer translation unit
// (lib.cpp). The decisions are flight_rules.h's; this reads and writes the
// rows: the flight, its contact events, the scheduled look at it, the ball row
// and the shot's consequences when the flight is final.
namespace {
std::vector<known_mover> known_movers(ReducerContext& ctx, const std::vector<snapshot_entry>& entries) {
    std::vector<known_mover> known;
    for (const snapshot_entry& entry : entries) {
        // A cart moves with its driver's steps.
        std::uint64_t stepper = entry.account;
        if (entry.kind == snapshot_kind::cart) {
            const std::optional<golf_cart_row> cart = ctx.db[golf_cart_owner].find(entry.account);
            stepper = cart ? cart->driver : 0;
        }
        const std::optional<motion_state_row> mover =
            stepper != 0 ? ctx.db[motion_state_account_id].find(stepper) : std::nullopt;
        known.push_back(known_mover{entry, mover ? mover->step_time_ms : 0});
    }
    return known;
}

// The course a flight is played on, and what it is flown with.
struct flight_world {
    const server_content& content;
    shot_course course;
    flight_env env() const { return flight_env{course, content.tuning, content.clubs}; }
};

flight_world flight_world_of(const server_course& course, const live_flight& live) {
    const server_content& content = *cached_content();
    return flight_world{content, server_shot_course(course, static_cast<std::size_t>(live.hole), live.record.setup.shot_at,
                                                   live.flags_out, content)};
}

void reschedule_flight(ReducerContext& ctx, const std::uint64_t flight_id, const std::int64_t due_ms) {
    std::vector<std::uint64_t> old;
    for (const flight_due_row& row : ctx.db[flight_due_flight_id].filter(flight_id)) {
        old.push_back(row.scheduled_id);
    }
    for (const std::uint64_t id : old) {
        ctx.db[flight_due_scheduled_id].delete_by_key(id);
    }
    ctx.db[flight_due].insert(flight_due_row{
        0, ScheduleAt::time(Timestamp::from_micros_since_epoch(due_ms * micros_per_millisecond)), flight_id});
}

// `row` as `live` now has it, written and scheduled for its next look.
void save_flight(ReducerContext& ctx, ball_flight_row row, const live_flight& live) {
    row.rests_ms = flight_end_ms(live.flight);
    row.boxes = flight_box_values(live.flight);
    row.pending_kind = live.pending.empty() ? 0 : static_cast<std::uint8_t>(live.pending.front().mover.kind);
    row.pending_account = live.pending.empty() ? 0 : live.pending.front().mover.account;
    row.state = encode_live_flight(live);
    ctx.db[ball_flight_id].update(row);
    reschedule_flight(ctx, row.id, next_due_ms(live, cached_content()->tuning.server));
}

void delete_flight(ReducerContext& ctx, const std::uint64_t flight_id) {
    std::vector<std::uint64_t> due;
    for (const flight_due_row& row : ctx.db[flight_due_flight_id].filter(flight_id)) {
        due.push_back(row.scheduled_id);
    }
    for (const std::uint64_t id : due) {
        ctx.db[flight_due_scheduled_id].delete_by_key(id);
    }
    ctx.db[ball_flight_id].delete_by_key(flight_id);
}

// The shooter's ball as the flight now has it: where it rests and when.
void sync_ball_to_flight(ReducerContext& ctx, const ball_flight_row& row, const live_flight& live) {
    std::optional<ball_row> ball = ctx.db[ball_account_id].find(row.account_id);
    if (!ball || ball->last_shot_at != live.record.setup.shot_at || ball->in_cup) {
        return;
    }
    const glm::vec3& rest = live.flight.result.rest_position;
    ball->x = rest.x;
    ball->y = rest.y;
    ball->z = rest.z;
    ball->rests_at = ball->rests_at +
        TimeDuration::from_micros((flight_end_ms(live.flight) - row.rests_ms) * micros_per_millisecond);
    ctx.db[ball_account_id].update(*ball);
}

// What the contacts just applied did: their rows for every client of the
// room, a daze starting when the ball met them, a knockout laying the body
// where it did.
void apply_contacts(ReducerContext& ctx, const ball_flight_row& row, const live_flight& live,
                    const std::vector<applied_contact>& applied) {
    for (const applied_contact& each : applied) {
        const flight_contact& contact = each.contact;
        ctx.db[ball_contact_event].insert(ball_contact_event_row{
            row.room_id, row.account_id, row.hole, row.stroke, each.index, contact.step,
            static_cast<std::uint8_t>(contact.mover.kind), contact.mover.position.x, contact.mover.position.y,
            contact.mover.position.z, contact.mover.yaw, contact.mover.shooter, contact.mover.account, contact.time,
            contact.speed_mps, contact.height_m});
    }
    for (const applied_contact& each : applied) {
        const flight_contact& contact = each.contact;
        const std::int64_t at_ms = live.flight.launch_ms + static_cast<std::int64_t>(std::llround(contact.time * 1000.0f));
        if (each.outcome == hit_outcome_kind::dazed) {
            start_dazed(ctx, contact.mover.account, at_ms);
        } else if (each.outcome == hit_outcome_kind::knocked_out) {
            knock_out_account(ctx, contact.mover.account, row.room_id, contact.mover.position, contact.mover.yaw);
        }
    }
}

// A shot's consequences, as `take_shot` had them at the swing before: the
// stroke and where the ball lies (lost in water: back, a stroke later; a ball
// hit on a green: two), or the hole's score and the round's and the course's
// completions; shot XP. Only for the ball the shot was hit from.
void finish_shot(ReducerContext& ctx, room_member_row member, const server_course& course, const live_flight& live) {
    std::optional<ball_row> ball = ctx.db[ball_account_id].find(member.account_id);
    const shot_setup& input = live.record.setup;
    if (!ball || ball->last_shot_at != input.shot_at || ball->in_cup) {
        return;
    }
    const std::uint64_t account = member.account_id;
    const server_content& content = *cached_content();
    const shot_result& result = live.flight.result;
    const golfer_progress_row owner = permanent_golfer(ctx, account);
    const save_data before = load_progress(ctx, owner);
    // Practice earns nothing: no score, no completion, and no XP where the place pays none.
    const bool practice = ball->kind != static_cast<std::uint8_t>(ball_kind::scored);
    progress_update update = practice || !course.course.rules.shot_xp
        ? progress_update{before, {}} : award_xp(before, content.rewards.shot, account_xp_effects(ctx, account));
    const award_event event{award_source_shot,
                            "shot:" + std::to_string(owner.golfer_id) + ":" + std::to_string(input.shot_at)};
    const std::int32_t strokes = live.stroke + result.penalty_strokes;
    const std::size_t hole = static_cast<std::size_t>(ball->hole);
    ball->x = result.rest_position.x;
    ball->y = result.rest_position.y;
    ball->z = result.rest_position.z;
    ball->stroke_count = strokes;
    if (!result.holed) {
        ctx.db[ball_account_id].update(*ball);
        store_progress(ctx, owner, before, update, event);
        return;
    }
    if (!practice) {
        ctx.db[hole_score].insert(hole_score_row{0, owner.golfer_id, course.course.id, ball->hole, strokes,
                                                 course.holes[hole].par, ctx.timestamp});
        update.progress = apply_hole_completed(update.progress);
        member.round_strokes.resize(course.holes.size(), 0);
        member.round_strokes[hole] = strokes;
        if (round_finished(current_round(member))) {
            // Retain the completed card until the next round's first tee-up.
            update.progress = apply_course_completed(update.progress, course.course.id);
        }
        store_progress(ctx, owner, before, update, event);
        ctx.db[room_member_account_id].update(member);
        if (auto booking = ctx.db[tee_reservation_account_id].find(account)) {
            booking->round_strokes = member.round_strokes;
            if (round_finished(current_round(member)) && !booking->completed) {
                release_slot_seat(ctx, *booking);
                booking->completed = true;
            }
            ++booking->revision;
            ctx.db[tee_reservation_reservation_id].update(*booking);
        }
    }
    // The ball stays in the cup until it is picked up, as offline; the player
    // stays where they holed out from.
    ball->in_cup = true;
    ctx.db[ball_account_id].update(*ball);
}

// Settles what is settled by `settled_until` and saves the flight, writing
// what the contacts did.
void settle_and_save(ReducerContext& ctx, const ball_flight_row& row, live_flight live, const std::int64_t settled_until,
                     const std::vector<known_mover>& movers, const flight_world& world) {
    const ball_flight_row before = row;
    const std::size_t pending = live.pending.size();
    const std::uint64_t pending_account = live.pending.empty() ? 0 : live.pending.front().mover.account;
    settlement settled = settle_flight(std::move(live), settled_until, movers, world.env());
    const bool changed = !settled.applied.empty() || settled.live.pending.size() != pending ||
        (!settled.live.pending.empty() && settled.live.pending.front().mover.account != pending_account);
    if (!changed) {
        return;
    }
    save_flight(ctx, before, settled.live);
    if (!settled.applied.empty()) {
        sync_ball_to_flight(ctx, before, settled.live);
        apply_contacts(ctx, before, settled.live, settled.applied);
    }
}

// The room's flights of one shooter at most each: `room_id`'s rows now.
std::vector<ball_flight_row> room_flights(ReducerContext& ctx, const std::uint64_t room_id) {
    std::vector<ball_flight_row> rows;
    for (const ball_flight_row& row : ctx.db[ball_flight_room_id].filter(room_id)) {
        rows.push_back(row);
    }
    return rows;
}

// A mover has played steps that left it as `path` says (a golfer's feet or a
// cart's pose, its time on the shared clock, `path.mover` where it ended):
// finds what it met of every flight in the room and settles what is settled.
// `gone`: the golfer is no longer on foot.
void flights_after_movement(ReducerContext& ctx, const room_member_row& member, const server_course& course, mover_path path,
                            const bool gone, const std::int64_t anchor_ms, const std::int64_t now_ms) {
    const std::vector<ball_flight_row> rows = room_flights(ctx, member.room_id);
    if (rows.empty() || path.samples.empty()) {
        return;
    }
    const game_tuning& tuning = cached_content()->tuning;
    std::optional<std::vector<known_mover>> others;
    std::optional<std::vector<known_mover>> everyone;
    for (const ball_flight_row& listed : rows) {
        const std::optional<ball_flight_row> row = ctx.db[ball_flight_id].find(listed.id);
        if (!row) {
            continue;
        }
        const bool owns_pending = row->pending_account == path.mover.account &&
            row->pending_kind == static_cast<std::uint8_t>(path.mover.kind) && row->pending_account != 0;
        const bool near = path_near_flight(row->boxes, row->shot_at, path, tuning);
        if (!near && !owns_pending) {
            continue;
        }
        std::optional<live_flight> live = decode_live_flight(row->state);
        if (!live) {
            delete_flight(ctx, row->id);
            continue;
        }
        if (!others) {
            others = std::vector<known_mover>();
            for (const known_mover& each : known_movers(ctx, room_movers(ctx, member.room_id, 0))) {
                if (!same_mover(each.entry, path.mover)) {
                    others->push_back(each);
                }
            }
            everyone = *others;
            if (!gone) {
                everyone->push_back(known_mover{path.mover, anchor_ms});
            }
        }
        mover_path mine = path;
        mine.mover.shooter = row->account_id == path.mover.account && path.mover.kind == snapshot_kind::person;
        const flight_world world = flight_world_of(course, *live);
        live = flight_after_movement(std::move(*live), std::move(mine), gone, *others, world.env());
        const std::vector<std::int64_t> stepped = stepped_times_near(row->boxes, *everyone, 5.0f);
        settle_and_save(ctx, *row, std::move(*live), settled_until_ms(stepped, now_ms, tuning.server), *everyone, world);
    }
}

// The shot is final: what its contacts did, what it came to for the shooter,
// and the flight gone.
void land_flight(ReducerContext& ctx, const ball_flight_row& row) {
    const std::optional<room_member_row> member = ctx.db[room_member_account_id].find(row.account_id);
    const server_course* course = member ? member_course(ctx, *member) : nullptr;
    std::optional<live_flight> live = decode_live_flight(row.state);
    if (!member || course == nullptr || !live || member->room_id != row.room_id) {
        delete_flight(ctx, row.id);
        return;
    }
    const flight_world world = flight_world_of(*course, *live);
    settlement settled = settle_flight(std::move(*live), now_unix_ms(ctx),
                                       known_movers(ctx, room_movers(ctx, row.room_id, 0)), world.env());
    // The row goes first: a knockout may end presence, which lands flights.
    delete_flight(ctx, row.id);
    sync_ball_to_flight(ctx, row, settled.live);
    apply_contacts(ctx, row, settled.live, settled.applied);
    const std::optional<room_member_row> fresh = ctx.db[room_member_account_id].find(row.account_id);
    if (fresh) {
        finish_shot(ctx, *fresh, *course, settled.live);
    }
}

void land_flight_of(ReducerContext& ctx, const std::uint64_t account_id, const bool ball_has_rested) {
    std::vector<ball_flight_row> rows;
    const std::int64_t slack_ms = static_cast<std::int64_t>(cached_content()->tuning.server.timing_slack_seconds * 1000.0f);
    for (const ball_flight_row& row : ctx.db[ball_flight_account_id].filter(account_id)) {
        if (!ball_has_rested || now_unix_ms(ctx) + slack_ms >= row.rests_ms) {
            rows.push_back(row);
        }
    }
    for (const ball_flight_row& row : rows) {
        land_flight(ctx, row);
    }
}

}

// The time to look at a shot in flight has come: a contact settles, or the
// ball is final.
SPACETIMEDB_REDUCER(settle_flight_due, ReducerContext ctx, flight_due_row due) {
    if (ctx.sender() != ctx.database_identity()) {
        return Err(error_internal_only);
    }
    const std::optional<ball_flight_row> row = ctx.db[ball_flight_id].find(due.flight_id);
    if (!row) {
        return Ok();
    }
    const server_content& content = *cached_content();
    const std::optional<live_flight> live = decode_live_flight(row->state);
    const std::optional<room_member_row> member = ctx.db[room_member_account_id].find(row->account_id);
    const server_course* course = member ? member_course(ctx, *member) : nullptr;
    if (!live || course == nullptr) {
        delete_flight(ctx, row->id);
        return Ok();
    }
    const std::int64_t now = now_unix_ms(ctx);
    if (now >= landing_due_ms(*live, content.tuning.server)) {
        land_flight(ctx, *row);
        return Ok();
    }
    const flight_world world = flight_world_of(*course, *live);
    const std::vector<known_mover> movers = known_movers(ctx, room_movers(ctx, row->room_id, 0));
    settle_and_save(ctx, *row, *live, settled_until_ms({}, now, content.tuning.server), movers, world);
    // Nothing settled yet (a clock a hair early): look again a moment on.
    if (ctx.db[ball_flight_id].find(row->id)) {
        bool scheduled = false;
        for (const flight_due_row& each : ctx.db[flight_due_flight_id].filter(row->id)) {
            scheduled = scheduled || each.scheduled_id != due.scheduled_id;
        }
        if (!scheduled) {
            reschedule_flight(ctx, row->id, std::max(now + 1, next_due_ms(*live, content.tuning.server)));
        }
    }
    return Ok();
}
