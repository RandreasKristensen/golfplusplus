#pragma once
// SDK schema for shots in flight and their contacts; included in lib.cpp only.

// A shot in flight until it is final (flight_rules.h). Private: the server
// keeps the flight's checkpoints, boxes and contacts as `state` (flight_codec.h)
// and the boxes again as plain numbers, so a mover's steps are ruled out
// without decoding it. One a shooter at a time; `rests_ms` is the shared clock
// when the ball rests as the flight is now.
struct ball_flight_row {
    std::uint64_t id;
    std::uint64_t room_id;
    std::uint64_t account_id;  // the shooter
    std::int32_t hole;
    std::int32_t stroke;
    std::int64_t shot_at;  // UTC milliseconds: the launch, with account and stroke the shot's identity
    std::int64_t rests_ms;
    std::vector<float> boxes;  // six a chunk: low, high
    // The mover of the pending contact (account 0: none), so its own steps
    // are looked at even when they take it far from the flight.
    std::uint8_t pending_kind;
    std::uint64_t pending_account;
    std::vector<std::uint8_t> state;
};
SPACETIMEDB_STRUCT(ball_flight_row, id, room_id, account_id, hole, stroke, shot_at, rests_ms, boxes, pending_kind, pending_account, state)
SPACETIMEDB_TABLE(ball_flight_row, ball_flight, Private)
FIELD_PrimaryKeyAutoInc(ball_flight, id)
FIELD_Index(ball_flight, room_id)
FIELD_Index(ball_flight, account_id)

// Looks at flight `flight_id` when a contact settles or the ball is final
// (settle_flight_due); rescheduled whenever the flight changes.
struct flight_due_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
    std::uint64_t flight_id;
};
SPACETIMEDB_STRUCT(flight_due_row, scheduled_id, scheduled_at, flight_id)
SPACETIMEDB_TABLE(flight_due_row, flight_due, Private)
FIELD_PrimaryKeyAutoInc(flight_due, scheduled_id)
FIELD_Index(flight_due, flight_id)
SPACETIMEDB_SCHEDULE(flight_due, 1, settle_flight_due)

// A ball in flight met a golfer or a cart (game/flight_cursor.h
// flight_contact), now settled: every client of the room replays the shot
// (account_id, hole, stroke) with its contacts in `index` order
// (replay_contacts) and blends to where the ball goes from here.
struct ball_contact_event_row {
    std::uint64_t room_id;
    std::uint64_t account_id;  // the shooter
    std::int32_t hole;
    std::int32_t stroke;
    std::int32_t index;  // among the shot's contacts, from 0
    std::int32_t step;   // the shot step the ball met it in
    std::uint8_t kind;   // snapshot_kind of what it met
    float x;
    float y;
    float z;
    float yaw;
    bool shooter;           // what it met is the shooter
    std::uint64_t account;  // a golfer's; a cart's owner's
    float time;             // seconds after launch
    float speed_mps;
    float height_m;
};
SPACETIMEDB_STRUCT(ball_contact_event_row, room_id, account_id, hole, stroke, index, step, kind, x, y, z, yaw, shooter, account, time,
                   speed_mps, height_m)
SPACETIMEDB_TABLE(ball_contact_event_row, ball_contact_event, Public, true)
FIELD_Index(ball_contact_event, room_id)
