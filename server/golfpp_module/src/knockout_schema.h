#pragma once
// SDK schema for knockouts and the bodies they leave; included in lib.cpp only.

// A knocked-out golfer lying where they fell (game/fallen_body.h): room-scoped
// and Public, so a room's clients see it. It belongs to the world, not to the
// golfer: leaving, reconnecting or deleting the account never removes it; only
// its body_expiry does, `expires_at` after it fell. Times are server microseconds.
struct body_row {
    std::uint64_t id;
    std::uint64_t account_id;
    std::uint64_t room_id;
    float x;  // the middle of the lying body, on the ground
    float y;
    float z;
    float yaw;  // the way their head points
    std::int64_t fell_at;
    std::int64_t expires_at;
    bool bag_on_back;
    std::uint8_t colour;  // favourite colour
    std::uint8_t hand;    // handedness
};
SPACETIMEDB_STRUCT(body_row, id, account_id, room_id, x, y, z, yaw, fell_at, expires_at, bag_on_back, colour, hand)
SPACETIMEDB_TABLE(body_row, body, Public)
FIELD_PrimaryKeyAutoInc(body, id)
FIELD_Index(body, room_id)
FIELD_Index(body, account_id)

// Deletes body `body_id` when it expires (expire_body).
struct body_expiry_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
    std::uint64_t body_id;
};
SPACETIMEDB_STRUCT(body_expiry_row, scheduled_id, scheduled_at, body_id)
SPACETIMEDB_TABLE(body_expiry_row, body_expiry, Private)
FIELD_PrimaryKeyAutoInc(body_expiry, scheduled_id)
FIELD_Index(body_expiry, body_id)
SPACETIMEDB_SCHEDULE(body_expiry, 1, expire_body)

// Tells a knocked-out golfer's connection why its session ended (the
// my_knockout_notice view): by identity, as a guest's account is deleted with
// the session. Deleted when that identity next connects or disconnects.
struct knockout_notice_row {
    Identity identity;
    Timestamp at;
};
SPACETIMEDB_STRUCT(knockout_notice_row, identity, at)
SPACETIMEDB_TABLE(knockout_notice_row, knockout_notice, Private)
FIELD_PrimaryKey(knockout_notice, identity)
