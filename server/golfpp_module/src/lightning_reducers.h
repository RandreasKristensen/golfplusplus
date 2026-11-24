#pragma once
// Lightning, SDK side, included by the single reducer translation unit
// (lib.cpp). The decisions are lightning_check_rules.h's; this reads the rooms
// and golfers, writes the strike rows and starts what a strike does.

// Every lightning.check_seconds: the golfers of each room in a thunderstorm
// roll the game's rule (lightning_rules.h) from the server's secret. A
// strike shows to the room (lightning_strike_event) and dazes the golfer from
// now or knocks them out where they stand.
// One strike on a golfer: it shows to their room (lightning_strike_event), then
// dazes them from `now_ms` or knocks them out where they stand (facing `yaw`).
// A check's strikes and a god command's both go through here.
void apply_lightning_hit(ReducerContext& ctx, const room_lightning_hit& each, const float yaw, const std::int64_t now_ms) {
    const glm::vec3& at = each.hit.position;
    ctx.db[lightning_strike_event].insert(
        lightning_strike_event_row{each.room_id, each.hit.account, at.x, at.y, at.z, ctx.timestamp});
    if (each.hit.outcome == lightning_outcome::knocked_out) {
        knock_out_account(ctx, each.hit.account, each.room_id, at, yaw);
    } else {
        start_dazed(ctx, each.hit.account, now_ms);
    }
}

SPACETIMEDB_REDUCER(check_lightning, ReducerContext ctx, lightning_run_row run) {
    static_cast<void>(run);
    if (ctx.sender() != ctx.database_identity()) {
        return Err(error_internal_only);
    }
    std::vector<lightning_room_ref> rooms;
    for (const room_row& each : ctx.db[room]) {
        if (each.player_count > 0) {
            rooms.push_back(lightning_room_ref{each.room_id, cached_course(each.course_id)});
        }
    }
    if (rooms.empty()) {
        return Ok();
    }
    std::map<std::uint64_t, float> yaws;
    const auto golfers_in = [&ctx, &yaws](const std::uint64_t room_id) {
        std::vector<lightning_golfer> golfers;
        for (const avatar_motion_row& row : ctx.db[avatar_motion_room_id].filter(room_id)) {
            golfers.push_back(lightning_golfer{row.account_id, glm::vec3(row.x, row.y, row.z),
                                               at_ball(static_cast<motion_mode>(row.mode))});
            yaws[row.account_id] = row.yaw;
        }
        return golfers;
    };
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    const std::string secret = config ? config->server_secret : std::string();
    const std::int64_t now_ms = now_unix_ms(ctx);
    const std::vector<room_lightning_hit> hits = check_lightning_rooms(
        rooms, golfers_in, *cached_content(), now_ms, server_shot_seed(secret, ctx.timestamp.micros_since_epoch(), 0, 0));
    for (const room_lightning_hit& each : hits) {
        apply_lightning_hit(ctx, each, yaws[each.hit.account], now_ms);
    }
    return Ok();
}

// A developer's command (the god menu's STRIKE ME): lightning comes down on
// the caller now, dazing or knocking them out, as a real strike does. Only a
// server whose owner allowed it (admin_set_config's allow_god).
SPACETIMEDB_REDUCER(god_strike, ReducerContext ctx, std::uint64_t expected_room, bool knock_out) {
    const std::optional<server_config_row> config = ctx.db[server_config_id].find(config_id);
    const std::string refusal = god_command_refusal(config && config->allow_god);
    if (!refusal.empty()) {
        return Err(refusal);
    }
    const member_result me = caller_member(ctx);
    if (!me.member) {
        return refused(me);
    }
    if (me.member->room_id != expected_room) { return Err(error_not_in_room); }
    const std::optional<avatar_motion_row> mover = ctx.db[avatar_motion_account_id].find(me.member->account_id);
    if (!mover) {
        return Err(error_not_in_room);
    }
    apply_lightning_hit(ctx, god_strike_hit(expected_room, me.member->account_id, glm::vec3(mover->x, mover->y, mover->z), knock_out), mover->yaw,
                        now_unix_ms(ctx));
    return Ok();
}
