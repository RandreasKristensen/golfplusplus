#pragma once
// SDK schema for lightning strikes; included in lib.cpp only.

// Lightning came down on a golfer in room `room_id` (check_lightning): every
// client of the room draws the bolt where they stood. An event row: Public,
// room-scoped, never kept.
struct lightning_strike_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;
    float x;
    float y;
    float z;
    Timestamp at;
};
SPACETIMEDB_STRUCT(lightning_strike_event_row, room_id, account_id, x, y, z, at)
SPACETIMEDB_TABLE(lightning_strike_event_row, lightning_strike_event, Public, true)
FIELD_Index(lightning_strike_event, room_id)

// Runs check_lightning every lightning.check_seconds: one row.
struct lightning_run_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
};
SPACETIMEDB_STRUCT(lightning_run_row, scheduled_id, scheduled_at)
SPACETIMEDB_TABLE(lightning_run_row, lightning_run, Private)
FIELD_PrimaryKeyAutoInc(lightning_run, scheduled_id)
SPACETIMEDB_SCHEDULE(lightning_run, 1, check_lightning)

// The check's run, unless the database has it already.
inline void schedule_lightning(ReducerContext& ctx) {
    if (ctx.db[lightning_run].count() != 0) {
        return;
    }
    const double seconds = static_cast<double>(cached_content()->tuning.lightning.check_seconds);
    ctx.db[lightning_run].insert(
        lightning_run_row{0, ScheduleAt::interval(TimeDuration::from_micros(static_cast<std::int64_t>(seconds * 1000000.0)))});
}
