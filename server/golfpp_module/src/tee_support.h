// Booking/travel adapters, included by the SDK's single reducer translation unit.
// Pure decisions live in tee_rules; all changes below share the reducer transaction.
namespace {
room_row course_room(ReducerContext& ctx, const std::string& course_id) {
    std::vector<room_candidate> rooms;
    for (const auto& room : ctx.db[room_course_id].filter(course_id)) {
        rooms.push_back({room.room_id, room.player_count, room.world_id});
    }
    if (const auto chosen = choose_room(rooms, course_world_id)) { return *ctx.db[room_room_id].find(*chosen); }
    return ctx.db[room].insert(room_row{0, course_id, 0, ctx.timestamp, course_world_id, static_cast<std::uint8_t>(course_id == online_hub_course ? place_kind::hub : place_kind::course)});
}
void release_slot_seat(ReducerContext& ctx, const tee_reservation_row& booking) {
    if (booking.completed) { return; }
    if (auto slot = ctx.db[tee_slot_slot_id].find(booking.slot_id)) {
        slot->booked_count -= 1;
        ctx.db[tee_slot_slot_id].update(*slot);
    }
}
void erase_reservation(ReducerContext& ctx, const tee_reservation_row& booking) {
    release_slot_seat(ctx, booking);
    if (auto member = ctx.db[room_member_account_id].find(booking.account_id)) {
        leave_group_state(ctx, *member);
        if (booking.started_at == 0) {
            member->round_started = false;
            ctx.db[room_member_account_id].update(*member);
            if (auto ball = ctx.db[ball_account_id].find(booking.account_id)) {
                ball->kind = static_cast<std::uint8_t>(ball_kind::warmup);
                ctx.db[ball_account_id].update(*ball);
            }
        }
    }
    std::vector<std::uint64_t> timers;
    for (const auto& timer : ctx.db[tee_expiry_reservation_id].filter(booking.reservation_id)) { timers.push_back(timer.scheduled_id); }
    for (const auto id : timers) { ctx.db[tee_expiry_scheduled_id].delete_by_key(id); }
    ctx.db[tee_reservation_reservation_id].delete_by_key(booking.reservation_id);
    // A time nobody holds any more has no row: the book lists it from the clock.
    const auto slot = ctx.db[tee_slot_slot_id].find(booking.slot_id);
    if (slot && slot->booked_count <= 0) {
        bool held = false;
        for (const auto& other : ctx.db[tee_reservation_slot_id].filter(slot->slot_id)) { (void)other; held = true; break; }
        if (!held) { ctx.db[tee_slot_slot_id].delete_by_key(slot->slot_id); }
    }
}
bool at_hub_anchor(ReducerContext& ctx, const room_member_row& member, const std::uint64_t room,
                    const bool book) {
    if (member.room_id != room) { return false; }
    const auto joined = ctx.db[room_room_id].find(room);
    const server_course* course = joined ? cached_course(joined->course_id) : nullptr;
    const auto mover = ctx.db[motion_state_account_id].find(member.account_id);
    if (!course || course->course.id != online_hub_course || !course->world.hub || !mover ||
        in_cart(static_cast<motion_mode>(mover->mode))) { return false; }
    if (!book && !course->area.bus) { return false; }
    const hub_anchor& anchor = book ? course->world.hub->book : *course->area.bus;
    return within_interact_reach(position_of(*mover), anchor.position, anchor.reach, cached_content()->tuning.server);
}
// A booked time's row, made on its first booking.
tee_slot_row ensure_tee_slot(ReducerContext& ctx, const std::string& course_id, const std::int64_t scheduled) {
    const std::string key = course_id + ":" + std::to_string(course_world_id) + ":" + std::to_string(scheduled);
    if (const auto slot = ctx.db[tee_slot_key].find(key)) { return *slot; }
    const room_row room = course_room(ctx, course_id);
    return ctx.db[tee_slot].insert(tee_slot_row{0, key, course_id, course_world_id, room.room_id, scheduled, 0});
}
void enter_course(ReducerContext& ctx, const std::uint64_t account, const server_course& course, const bool takes_cart) {
    leave_room_state(ctx, account);
    ensure_loadout(ctx, account);
    room_row joined = course_room(ctx, course.course.id);
    joined.player_count += 1;
    ctx.db[room_room_id].update(joined);
    const glm::vec3 spawn = anchor_on_terrain(course.area, course.world.hub ? course.world.hub->book.position : (course.hub ? course.hub->arrival : course.world.hole_starts.front().position));
    auto booking = ctx.db[tee_reservation_account_id].find(account);
    auto slot = booking ? ctx.db[tee_slot_slot_id].find(booking->slot_id) : std::optional<tee_slot_row>{};
    const bool booked = slot && slot->room_id == joined.room_id;
    const std::uint64_t group_id = booked && booking->grouped ? group_of_slot(slot->slot_id) : 0;
    if (group_id != 0) {
        if (auto group = ctx.db[golf_group_group_id].find(group_id)) {
            group->member_count += 1;
            ctx.db[golf_group_group_id].update(*group);
        } else { ctx.db[golf_group].insert(golf_group_row{group_id, joined.room_id, 1}); }
    }
    const std::vector<std::int32_t> scores = booked ? booking->round_strokes : std::vector<std::int32_t>(course.holes.size(), 0);
    ctx.db[room_member].insert(room_member_row{account, joined.room_id, group_id, scores, booked && booking->started_at != 0});
    ctx.db[motion_state].insert(joined_motion_state(account, spawn, ctx.timestamp));
    avatar_motion_row avatar{account, joined.room_id, static_cast<std::uint8_t>(motion_mode::idle),
        spawn.x, spawn.y, spawn.z, 0.0f, 0.0f, 0.0f, 0.0, 0, 0, 0, 0, false, ctx.timestamp, 0, 0, {}};
    index_motion(avatar);
    ctx.db[avatar_motion].insert(avatar);
    bag_where bag = bag_where::on_back;
    if (course.course.carts && takes_cart) {
        const cart_pose cart = free_parking(ctx, joined.room_id, course);
        golf_cart_row row{account, joined.room_id, cart.position.x, cart.position.y, cart.position.z,
            cart.yaw, 0, static_cast<std::uint8_t>(cart_condition::upright), 0, 0.0, 0, 0, {}};
        index_motion(row);
        ctx.db[golf_cart].insert(row);
        bag = bag_where::on_cart;
    }
    ctx.db[player_bag].insert(player_bag_row{account, joined.room_id, static_cast<std::uint8_t>(bag),
        static_cast<std::uint8_t>(cart_seat::driver), 0, 0.0f, 0.0f, 0.0f, 0.0f, cached_content()->clubs.front().id});
}
}

