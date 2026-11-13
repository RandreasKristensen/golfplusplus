#pragma once
// SDK schema for the latest paired greeting; included in lib.cpp only.
struct dap_row {
    std::uint64_t id;
    std::uint64_t room_id;
    std::uint64_t first;
    std::uint64_t second;
    std::int64_t started_at;
    float first_x; float first_y; float first_z;
    float second_x; float second_y; float second_z;
};
SPACETIMEDB_STRUCT(dap_row, id, room_id, first, second, started_at, first_x, first_y, first_z, second_x, second_y, second_z)
SPACETIMEDB_TABLE(dap_row, dap_pair, Public)
FIELD_PrimaryKeyAutoInc(dap_pair, id)
FIELD_Index(dap_pair, room_id)
FIELD_Index(dap_pair, first)
FIELD_Index(dap_pair, second)
