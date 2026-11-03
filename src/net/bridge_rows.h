#pragma once

// Reading what the bridge (net/stdb_bridge.h) hands over: its strings as
// std::string, and a row change applied to rows kept by id.

#include "net/stdb_bridge.h"

#include <cstdint>
#include <functional>
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

// A view's row (a my_* view): it has no key, so a change comes as the old
// row deleted and the new one inserted, in either order. A delete only
// takes the row out while it still is the deleted value.
template <typename Map, typename Equal = std::equal_to<typename Map::mapped_type>>
void apply_view_change(Map& rows, const std::uint64_t id, const stdb_row_change change, typename Map::mapped_type value,
                       Equal same = {}) {
    const auto found = rows.find(id);
    if (change != STDB_ROW_DELETE) {
        rows[id] = std::move(value);
    } else if (found != rows.end() && same(found->second, value)) {
        rows.erase(found);
    }
}
