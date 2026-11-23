// Booking schema registrations, included in lib.cpp's single SDK translation unit.
// Slot identity is also group identity; reservations survive ordinary disconnects.
struct tee_slot_row {
    std::uint64_t slot_id;
    std::string key;
    std::string course_id;
    std::uint32_t world_id;
    std::uint64_t room_id;
    std::int64_t scheduled_at; // UTC microseconds
    std::int32_t booked_count;
    bool departed; // its bus has left (the tee time came): off the departure board
};
SPACETIMEDB_STRUCT(tee_slot_row, slot_id, key, course_id, world_id, room_id, scheduled_at, booked_count, departed)
SPACETIMEDB_TABLE(tee_slot_row, tee_slot, Public)
FIELD_PrimaryKeyAutoInc(tee_slot, slot_id)
FIELD_Unique(tee_slot, key)
FIELD_Index(tee_slot, course_id)
FIELD_Index(tee_slot, departed)

struct tee_reservation_row {
    std::uint64_t reservation_id;
    std::uint64_t account_id;
    std::uint64_t slot_id;
    std::int64_t started_at; // actual first strike, zero until then
    std::uint64_t revision;
    std::uint32_t lane;
    bool grouped;
    bool completed;
    bool takes_cart; // chosen with the booking: my own cart for the round (where the course has them)
    std::vector<std::int32_t> round_strokes;
};
SPACETIMEDB_STRUCT(tee_reservation_row, reservation_id, account_id, slot_id, started_at, revision, lane, grouped, completed, takes_cart, round_strokes)
SPACETIMEDB_TABLE(tee_reservation_row, tee_reservation, Private)
FIELD_PrimaryKeyAutoInc(tee_reservation, reservation_id)
FIELD_Unique(tee_reservation, account_id)
FIELD_Index(tee_reservation, slot_id)

// Durable audit, never deleted on booking changes, completion or disconnect.
struct tee_start_row {
    std::uint64_t reservation_id;
    std::uint64_t account_id;
    std::uint64_t slot_id;
    std::string course_id;
    std::uint32_t world_id;
    std::int64_t scheduled_at;
    std::int64_t started_at;
    std::int64_t lateness;
};
SPACETIMEDB_STRUCT(tee_start_row, reservation_id, account_id, slot_id, course_id, world_id, scheduled_at, started_at, lateness)
SPACETIMEDB_TABLE(tee_start_row, tee_start, Private)
FIELD_PrimaryKey(tee_start, reservation_id)
FIELD_Index(tee_start, account_id)

struct tee_expiry_row {
    std::uint64_t scheduled_id;
    ScheduleAt scheduled_at;
    std::uint64_t reservation_id;
};
SPACETIMEDB_STRUCT(tee_expiry_row, scheduled_id, scheduled_at, reservation_id)
SPACETIMEDB_TABLE(tee_expiry_row, tee_expiry, Private)
FIELD_PrimaryKeyAutoInc(tee_expiry, scheduled_id)
FIELD_Index(tee_expiry, reservation_id)
SPACETIMEDB_SCHEDULE(tee_expiry, 1, expire_tee)

// A book request's acknowledgement/clock, scoped to the account. A refresh
// materializes only this course's next day of slots, never a table subscription.
// `request_id` is the client's own number for the request being answered.
struct tee_book_status_row {
    std::uint64_t account_id;
    std::string course_id;
    std::int64_t server_now;
    std::uint64_t request_id;
};
SPACETIMEDB_STRUCT(tee_book_status_row, account_id, course_id, server_now, request_id)
SPACETIMEDB_TABLE(tee_book_status_row, tee_book_status, Private)
FIELD_PrimaryKey(tee_book_status, account_id)

struct tee_reservation_view {
    std::uint64_t reservation_id;
    std::uint64_t slot_id;
    std::int64_t started_at;
    bool grouped;
    bool completed;
    std::uint64_t revision;
    std::string course_id;
    std::uint32_t world_id;
    std::uint64_t room_id;
    std::int64_t scheduled_at;
    bool takes_cart;
};
SPACETIMEDB_STRUCT(tee_reservation_view, reservation_id, slot_id, started_at, grouped, completed, revision, course_id, world_id, room_id, scheduled_at, takes_cart)

// One booked group on the hub's departure board: its time, course and names.
struct departure_view {
    std::uint64_t slot_id;
    std::string course_id;
    std::int64_t scheduled_at;
    std::vector<std::string> names;
};
SPACETIMEDB_STRUCT(departure_view, slot_id, course_id, scheduled_at, names)
