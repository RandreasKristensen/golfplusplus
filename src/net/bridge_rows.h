#pragma once

// Reading what the bridge (net/stdb_bridge.h) hands over: its strings as
// std::string, and a row change applied to rows kept by id.

#include "net/stdb_bridge.h"

#include <cstdint>
#include <string>
#include <utility>

inline std::string text_of(const stdb_string& text) {
    return text.data != nullptr ? std::string(text.data, text.len) : std::string();
}

// A deleted row leaves `rows`; an inserted or updated one is `value`.
template <typename Map>
void apply_change(Map& rows, const std::uint64_t id, const stdb_row_change change, typename Map::mapped_type value) {
    if (change == STDB_ROW_DELETE) {
        rows.erase(id);
    } else {
        rows[id] = std::move(value);
    }
}
