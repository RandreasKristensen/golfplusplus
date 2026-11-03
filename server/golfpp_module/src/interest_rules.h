#pragma once

// Sparse motion interest in spatial cells, independent of hole order or the
// ball in play. Retained accounts (self, group and occupied seats) stay
// visible anywhere in the same room. Callers supply indexed reads.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

struct motion_cell {
    std::int32_t x = 0;
    std::int32_t z = 0;
};

// The C++ SDK's read-only view context exposes single-column indexes.
// Keep the room and both signed coordinates in one unambiguous key.
inline std::string motion_area_key(const std::uint64_t room, const motion_cell area) {
    return std::to_string(room) + ":" + std::to_string(area.x) + ":" + std::to_string(area.z);
}

inline motion_cell motion_area(const glm::vec3& position, const float cell_width) {
    const auto coordinate = [cell_width](const float value) {
        // Leave room for neighbouring cells, even for a malformed position.
        const double cell = std::floor(static_cast<double>(value) / cell_width);
        return static_cast<std::int32_t>(std::clamp(cell,
            static_cast<double>(std::numeric_limits<std::int32_t>::min()) + 1.0,
            static_cast<double>(std::numeric_limits<std::int32_t>::max()) - 1.0));
    };
    return motion_cell{coordinate(position.x), coordinate(position.z)};
}

template <typename GroupMembers, typename OwnCart, typename RiddenCarts>
std::set<std::uint64_t> retained_motion_accounts(const std::uint64_t account, const std::uint64_t room,
                                                const std::uint64_t group, GroupMembers&& group_members,
                                                OwnCart&& own_cart, RiddenCarts&& ridden_carts) {
    std::set<std::uint64_t> accounts{account};
    if (group != 0) {
        for (const auto& member : group_members(group)) {
            if (member.room_id == room) {
                accounts.insert(member.account_id);
            }
        }
    }
    const std::set<std::uint64_t> companions = accounts;
    for (const std::uint64_t companion : companions) {
        if (const auto cart = own_cart(companion)) {
            if (cart->room_id == room && cart->passenger != 0) {
                accounts.insert(cart->passenger);
            }
        }
        for (const auto& cart : ridden_carts(companion)) {
            if (cart.room_id == room) {
                accounts.insert(cart.owner);
            }
        }
    }
    return accounts;
}

template <typename Row, typename InArea, typename ByAccount, typename Key>
std::vector<Row> visible_motion_rows(const std::uint64_t room, const motion_cell area,
                                     const std::set<std::uint64_t>& retained, InArea&& in_area,
                                     ByAccount&& by_account, Key&& key) {
    std::map<std::uint64_t, Row> rows;
    const auto add = [&](const Row& row) {
        if (row.room_id == room) {
            rows.insert_or_assign(key(row), row);
        }
    };
    for (std::int32_t dx = -1; dx <= 1; ++dx) {
        for (std::int32_t dz = -1; dz <= 1; ++dz) {
            for (const Row& row : in_area(motion_cell{area.x + dx, area.z + dz})) {
                add(row);
            }
        }
    }
    for (const std::uint64_t account : retained) {
        if (const auto row = by_account(account)) {
            add(*row);
        }
    }
    std::vector<Row> out;
    out.reserve(rows.size());
    for (const auto& [account, row] : rows) {
        out.push_back(row);
    }
    return out;
}
