#pragma once
// Paired authoritative greeting transaction. Awards are immutable and identify
// both the pair and recipient; no warmup/hub shot-XP gate applies to Social.
SPACETIMEDB_REDUCER(dap, ReducerContext ctx, std::uint64_t expected_room, std::uint64_t target) {
    const auto me = caller_member(ctx);
    if (!me.member) { return refused(me); }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const auto other = ctx.db[room_member_account_id].find(target);
    if (!other || other->room_id != expected_room) { return Err(error_not_in_room); }
    const auto participant = [&](const room_member_row& member) {
        const auto mover = ctx.db[motion_state_account_id].find(member.account_id);
        const auto ball = ctx.db[ball_account_id].find(member.account_id);
        const auto avatar = ctx.db[avatar_motion_account_id].find(member.account_id);
        bool busy = (avatar && avatar->crouched) || !mover || !mover->grounded || dap_busy(ctx, member.account_id) ||
            (ball && ball->rests_at > ctx.timestamp);
        for (const auto& use : ctx.db[item_use_account_id].filter(member.account_id)) {
            const auto item = cached_content()->items.find(use.item_id);
            busy = busy || (item && item->use && now_unix_ms(ctx) - use.used_at_ms <
                static_cast<std::int64_t>(item->use->seconds_per_use * 1000.0f));
        }
        return dap_participant{member.account_id, member.room_id, mover ? position_of(*mover) : glm::vec3(0.0f),
            mover ? static_cast<motion_mode>(mover->mode) : motion_mode::idle, busy};
    };
    const auto first = participant(*me.member);
    const auto second = participant(*other);
    const auto error = check_dap(first, second, cached_content()->tuning);
    if (!error.empty()) { return Err(error); }
    erase_daps(ctx, first.account); erase_daps(ctx, second.account);
    const auto row = ctx.db[dap_pair].insert(dap_row{0, expected_room, first.account, second.account,
        ctx.timestamp.micros_since_epoch(), first.position.x, first.position.y, first.position.z,
        second.position.x, second.position.y, second.position.z});
    for (const auto account : {first.account, second.account}) {
        // Stop extrapolation immediately, including for a paused recipient.
        if (auto avatar = ctx.db[avatar_motion_account_id].find(account)) {
            avatar->speed = 0.0f; avatar->turn_rate = 0.0f;
            avatar->mode = static_cast<std::uint8_t>(motion_mode::idle);
            avatar->server_time = ctx.timestamp;
            ctx.db[avatar_motion_account_id].update(*avatar);
        }
        const auto owner = permanent_golfer(ctx, account);
        const auto before = load_progress(ctx, owner);
        store_progress(ctx, owner, before,
            award_xp(before, cached_content()->rewards.dap, account_xp_effects(ctx, account)),
            award_event{"dap", dap_award_key(row.id, owner.golfer_id)});
    }
    return Ok();
}
