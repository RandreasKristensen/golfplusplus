// Tee book, bus and first-tee adapters, included in lib.cpp's SDK translation unit.
// All row mutations share the reducer transaction; tee_rules makes the decisions.
#include "tee_support.h"

// Entry/reconnect only. An existing member cannot switch courses with this reducer.
SPACETIMEDB_REDUCER(join_course, ReducerContext ctx, std::string course_id, bool takes_cart) {
    const caller_result me = caller(ctx);
    if (!me.player) { return Err(me.error); }
    if (me.player->display_name.empty()) { return Err(error_needs_name); }
    const auto account = me.player->account_id;
    if (auto member = ctx.db[room_member_account_id].find(account)) {
        const auto room = ctx.db[room_room_id].find(member->room_id);
        return room && room->course_id == course_id ? Ok() : Err(error_hub_only);
    }
    // All new connections enter the hub, even with a retained reservation.
    if (course_id != online_hub_course) { return Err(error_hub_only); }
    const server_course* course = cached_course(course_id);
    if (!course || !course->world.hub) { return Err(error_unknown_course); }
    enter_course(ctx, account, *course, takes_cart);
    return Ok();
}
SPACETIMEDB_REDUCER(leave_room, ReducerContext ctx, std::uint64_t expected_room) {
    const caller_result me = caller(ctx);
    if (!me.player) { return Err(me.error); }
    const auto member = ctx.db[room_member_account_id].find(me.player->account_id);
    if (member && member->room_id != expected_room) { return Err(error_not_in_room); }
    leave_room_state(ctx, me.player->account_id);
    return Ok();
}
SPACETIMEDB_REDUCER(open_tee_book, ReducerContext ctx, std::uint64_t room_id, std::string course_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (!at_hub_anchor(ctx, *me.member, room_id, true)) { return Err(error_hub_only); }
    const server_course* course = cached_course(course_id);
    if (!course || course->kind == place_kind::hub) { return Err(error_unknown_course); }
    // The times are the clock's (game/tee_times.h); only booked ones have rows.
    const auto now = ctx.timestamp.micros_since_epoch();
    const tee_book_status_row status{me.member->account_id, course_id, now};
    if (ctx.db[tee_book_status_account_id].find(status.account_id)) { ctx.db[tee_book_status_account_id].update(status); }
    else { ctx.db[tee_book_status].insert(status); }
    return Ok();
}
SPACETIMEDB_REDUCER(book_tee, ReducerContext ctx, std::uint64_t room_id, std::uint64_t expected_booking,
                    std::string course_id, std::int64_t scheduled_at) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (!at_hub_anchor(ctx, *me.member, room_id, true)) { return Err(error_hub_only); }
    const server_course* course = cached_course(course_id);
    if (!course || course->kind == place_kind::hub) { return Err(error_unknown_course); }
    const auto current = ctx.db[tee_reservation_account_id].find(me.member->account_id);
    const auto current_slot = current ? ctx.db[tee_slot_slot_id].find(current->slot_id) : std::optional<tee_slot_row>{};
    const std::string key = course_id + ":" + std::to_string(course_world_id) + ":" + std::to_string(scheduled_at);
    const auto existing = ctx.db[tee_slot_key].find(key);
    const bool same = existing && current_slot && existing->slot_id == current_slot->slot_id;
    const std::string error = check_booking(expected_booking, current ? current->reservation_id : 0, scheduled_at,
        ctx.timestamp.micros_since_epoch(), existing ? existing->booked_count : 0, same, cached_content()->tuning.server);
    if (!error.empty()) { return Err(error); }
    if (same) { return Ok(); }
    if (current) { erase_reservation(ctx, *current); }
    tee_slot_row slot = ensure_tee_slot(ctx, course_id, scheduled_at);
    std::set<std::uint32_t> lanes;
    for (const auto& other : ctx.db[tee_reservation_slot_id].filter(slot.slot_id)) { lanes.insert(other.lane); }
    std::uint32_t lane = 0;
    while (lanes.count(lane) != 0) { ++lane; }
    const auto booking = ctx.db[tee_reservation].insert(tee_reservation_row{0, me.member->account_id, slot.slot_id,
        0, 1, lane, true, false, std::vector<std::int32_t>(course->holes.size(), 0)});
    slot.booked_count += 1;
    ctx.db[tee_slot_slot_id].update(slot);
    // Checked when the bus leaves (tee_missed) and when the first strike is due.
    const auto expires = Timestamp::from_micros_since_epoch(scheduled_at + cached_content()->tuning.server.tee_expiry_minutes * minute_micros);
    ctx.db[tee_expiry].insert(tee_expiry_row{0, ScheduleAt::time(Timestamp::from_micros_since_epoch(scheduled_at)), booking.reservation_id});
    ctx.db[tee_expiry].insert(tee_expiry_row{0, ScheduleAt::time(expires), booking.reservation_id});
    return Ok();
}
SPACETIMEDB_REDUCER(cancel_tee, ReducerContext ctx, std::uint64_t room_id, std::uint64_t reservation_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (!at_hub_anchor(ctx, *me.member, room_id, true)) { return Err(error_hub_only); }
    const auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id);
    if (!booking || booking->reservation_id != reservation_id) { return Err(error_stale_booking); }
    erase_reservation(ctx, *booking);
    return Ok();
}
SPACETIMEDB_REDUCER(expire_tee, ReducerContext ctx, tee_expiry_row timer) {
    if (ctx.sender() != ctx.database_identity()) { return Err(error_internal_only); }
    const auto booking = ctx.db[tee_reservation_reservation_id].find(timer.reservation_id);
    const auto slot = booking ? ctx.db[tee_slot_slot_id].find(booking->slot_id) : std::optional<tee_slot_row>{};
    const auto member = booking ? ctx.db[room_member_account_id].find(booking->account_id) : std::optional<room_member_row>{};
    const bool boarded = member && slot && member->room_id == slot->room_id;
    if (booking && slot && tee_missed(slot->scheduled_at, ctx.timestamp.micros_since_epoch(), booking->started_at != 0, boarded,
                                      cached_content()->tuning.server)) { erase_reservation(ctx, *booking); }
    return Ok();
}
SPACETIMEDB_REDUCER(take_bus, ReducerContext ctx, std::uint64_t room_id, std::uint64_t reservation_id, bool takes_cart) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (!at_hub_anchor(ctx, *me.member, room_id, false)) { return Err(error_hub_only); }
    const auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id);
    if (!booking || booking->reservation_id != reservation_id) { return Err(error_stale_booking); }
    if (booking->completed) { return Err(error_booking_required); }
    const auto slot = ctx.db[tee_slot_slot_id].find(booking->slot_id);
    if (!slot) { return Err(error_booking_required); }
    const auto error = check_bus(slot->scheduled_at, ctx.timestamp.micros_since_epoch(), booking->started_at != 0,
                                 cached_content()->tuning.server);
    if (!error.empty()) { return Err(error); }
    const server_course* course = cached_course(slot->course_id);
    if (!course) { return Err(error_unknown_course); }
    enter_course(ctx, me.member->account_id, *course, takes_cart);
    return Ok();
}
SPACETIMEDB_REDUCER(return_to_hub, ReducerContext ctx, std::uint64_t room_id) {
    const member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    const auto course = member_course(ctx, *me.member);
    // Pause-menu return is allowed anywhere, including a cart. Finished scores
    // belong to progress; leaving this place discards only the active round.
    if (me.member->room_id != room_id || !course || course->kind != place_kind::course) { return Err(error_unknown_course); }
    const auto hub = cached_course(online_hub_course);
    if (!hub) { return Err(error_unknown_course); }
    // Back in the hub a booking played (started or finished) is done, and one
    // past its time has missed its bus; one not yet due still has its bus.
    if (const auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id)) {
        const auto slot = ctx.db[tee_slot_slot_id].find(booking->slot_id);
        if (!slot || booking->started_at != 0 || booking->completed ||
            tee_missed(slot->scheduled_at, ctx.timestamp.micros_since_epoch(), false, false, cached_content()->tuning.server)) {
            erase_reservation(ctx, *booking);
        }
    }
    enter_course(ctx, me.member->account_id, *hub, false);
    return Ok();
}
SPACETIMEDB_REDUCER(start_booked_round, ReducerContext ctx, std::uint64_t room_id, std::uint64_t reservation_id) {
    member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (dap_busy(ctx, me.member->account_id)) { return Err(error_not_available); }
    const auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id);
    if (!booking || booking->reservation_id != reservation_id || booking->completed) { return Err(error_stale_booking); }
    const auto slot = ctx.db[tee_slot_slot_id].find(booking->slot_id);
    if (!slot || slot->room_id != room_id || me.member->room_id != room_id || me.member->round_started) {
        return Err(error_stale_booking);
    }
    const auto now = ctx.timestamp.micros_since_epoch();
    // Another group's ball on hole 1 keeps the tee from being clear; one still
    // unstruck there also holds the tee itself.
    bool tee_clear = true;
    bool tee_occupied = false;
    for (const auto& other : ctx.db[ball_room_id].filter(room_id)) {
        const auto member = ctx.db[room_member_account_id].find(other.account_id);
        if (other.account_id != me.member->account_id && other.kind == static_cast<std::uint8_t>(ball_kind::scored) &&
            other.hole == 0 && (!member || member->group_id != group_of_slot(slot->slot_id))) {
            tee_clear = false;
            tee_occupied = tee_occupied || other.stroke_count == 0;
        }
    }
    for (const auto& earlier : ctx.db[tee_slot_course_id].filter(slot->course_id)) {
        if (earlier.world_id != slot->world_id || earlier.scheduled_at >= slot->scheduled_at) { continue; }
        for (const auto& waiting : ctx.db[tee_reservation_slot_id].filter(earlier.slot_id)) {
            if (waiting.started_at == 0 && !tee_expired(earlier.scheduled_at, now, false, cached_content()->tuning.server)) {
                tee_clear = false;
            }
        }
    }
    const auto error = check_first_tee(slot->scheduled_at, now, 0, booking->started_at != 0, tee_clear,
                                       cached_content()->tuning.server);
    if (!error.empty()) { return Err(error); }
    const auto course = member_course(ctx, *me.member);
    const auto mover = ctx.db[motion_state_account_id].find(me.member->account_id);
    if (!course || !mover || in_cart(static_cast<motion_mode>(mover->mode)) || !within_interact_reach(position_of(*mover),
        course->world.hole_starts.front().position, course->world.hole_starts.front().interaction_radius,
        cached_content()->tuning.server)) { return Err(error_too_far); }
    // Each booking has its own stable tee lane: no tee_in_use deadlock and
    // no overwriting another ball while all five prepare together.
    if (tee_occupied) { return Err(error_tee_in_use); }
    const auto& tuning = cached_content()->tuning;
    glm::vec3 tee = booked_tee_position(course->holes.front().tee_position, course->area.cups.front().centre,
                                        booking->lane, tuning);
    tee = resting_ball_position(course->area, tee, tuning.scale.ball_physics_radius_meters);
    const ball_row teed{me.member->account_id, room_id, 0, tee.x, tee.y, tee.z, 0, 0, ctx.timestamp, false, static_cast<std::uint8_t>(ball_kind::scored), -1};
    if (ctx.db[ball_account_id].find(me.member->account_id)) { ctx.db[ball_account_id].update(teed); }
    else { ctx.db[ball].insert(teed); }
    me.member->round_started = true;
    me.member->round_strokes = tee_round_strokes(booking->round_strokes, course->holes.size());
    ctx.db[room_member_account_id].update(*me.member);
    place_player(ctx, *me.member, tee_stance_position(course->area, tee, course->area.cups.front().centre,
        tuning.player.ball_stand_off_distance));
    return Ok();
}
SPACETIMEDB_REDUCER(leave_group, ReducerContext ctx, std::uint64_t room_id, std::uint64_t reservation_id) {
    member_result me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (me.member->room_id != room_id || me.member->group_id == 0) { return Err(error_not_in_group); }
    auto booking = ctx.db[tee_reservation_account_id].find(me.member->account_id);
    if (!booking || booking->reservation_id != reservation_id) { return Err(error_stale_booking); }
    if (booking->started_at == 0) { erase_reservation(ctx, *booking); }
    else {
        booking->grouped = false;
        ++booking->revision;
        ctx.db[tee_reservation_reservation_id].update(*booking);
        leave_group_state(ctx, *me.member);
    }
    return Ok();
}
SPACETIMEDB_VIEW(std::optional<tee_reservation_view>, my_tee_reservation, Public, ViewContext ctx) {
    const auto login = ctx.db[account_login_identity].find(ctx.sender());
    const auto booking = login ? ctx.db[tee_reservation_account_id].find(login->account_id) : std::optional<tee_reservation_row>{};
    const auto slot = booking ? ctx.db[tee_slot_slot_id].find(booking->slot_id) : std::optional<tee_slot_row>{};
    if (!booking || !slot) { return std::nullopt; }
    return tee_reservation_view{booking->reservation_id, booking->slot_id, booking->started_at, booking->grouped,
        booking->completed, booking->revision, slot->course_id, slot->world_id, slot->room_id, slot->scheduled_at};
}
SPACETIMEDB_VIEW(std::optional<tee_book_status_row>, my_tee_book_status, Public, ViewContext ctx) {
    const auto login = ctx.db[account_login_identity].find(ctx.sender());
    return login ? ctx.db[tee_book_status_account_id].find(login->account_id) : std::nullopt;
}

SPACETIMEDB_VIEW(std::vector<tee_slot_row>, my_tee_slots, Public, ViewContext ctx) {
    const auto login = ctx.db[account_login_identity].find(ctx.sender());
    const auto status = login ? ctx.db[tee_book_status_account_id].find(login->account_id) : std::optional<tee_book_status_row>{};
    std::vector<tee_slot_row> slots;
    if (status) {
        const auto horizon = status->server_now + cached_content()->tuning.server.tee_book_days * 24 * 60 * minute_micros;
        for (const auto& slot : ctx.db[tee_slot_course_id].filter(status->course_id)) {
            if (slot.scheduled_at >= status->server_now && slot.scheduled_at <= horizon) { slots.push_back(slot); }
        }
    }
    return slots;
}
