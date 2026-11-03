#include "doctest.h"

#include "interest_rules.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace {
struct motion_row {
    std::uint64_t account_id;
    std::uint64_t room_id;
    motion_cell area;
};

// The same indexed reads used by both module views, with seek counts so a
// room's population cannot silently turn into a scan.
struct motion_index {
    std::map<std::uint64_t, motion_row> accounts;
    std::map<std::tuple<std::uint64_t, std::int32_t, std::int32_t>, std::vector<motion_row>> areas;
    int area_reads = 0;
    int account_reads = 0;

    void insert(const motion_row& row) {
        accounts.insert_or_assign(row.account_id, row);
        areas[{row.room_id, row.area.x, row.area.z}].push_back(row);
    }

    std::vector<motion_row> visible(const std::uint64_t room, const motion_cell area,
                                    const std::set<std::uint64_t>& retained) {
        area_reads = 0;
        account_reads = 0;
        return visible_motion_rows<motion_row>(room, area, retained,
            [&](const motion_cell cell) {
                ++area_reads;
                return areas[{room, cell.x, cell.z}];
            },
            [&](const std::uint64_t account) -> std::optional<motion_row> {
                ++account_reads;
                const auto found = accounts.find(account);
                return found != accounts.end() ? std::optional<motion_row>(found->second) : std::nullopt;
            },
            [](const motion_row& row) { return row.account_id; });
    }
};

std::set<std::uint64_t> ids(const std::vector<motion_row>& rows) {
    std::set<std::uint64_t> out;
    for (const motion_row& row : rows) {
        out.insert(row.account_id);
    }
    return out;
}
}

TEST_CASE("motion interest: spatial cells include negative coordinates and exact boundaries") {
    const motion_cell origin = motion_area(glm::vec3(0.0f, 2000.0f, 99.99f), 100.0f);
    CHECK(origin.x == 0);
    CHECK(origin.z == 0);
    const motion_cell edge = motion_area(glm::vec3(-0.01f, 0.0f, 100.0f), 100.0f);
    CHECK(edge.x == -1);
    CHECK(edge.z == 1);
    const motion_cell negative = motion_area(glm::vec3(-100.0f, 0.0f, -100.01f), 100.0f);
    CHECK(negative.x == -1);
    CHECK(negative.z == -2);
    const motion_cell scaled = motion_area(glm::vec3(25.0f, 0.0f, -0.1f), 100.0f / 4.0f);
    CHECK(scaled.x == 1);
    CHECK(scaled.z == -1);
    CHECK(motion_area_key(7, edge) == "7:-1:1");
    CHECK(motion_area_key(7, {1, 23}) != motion_area_key(7, {12, 3}));
    CHECK(motion_area_key(7, {1, 2}) != motion_area_key(71, {0, 2}));
}

TEST_CASE("motion interest: neighbours and retained accounts appear once, only in this room") {
    motion_index index;
    index.insert({1, 7, {0, 0}});     // self, also in the area
    index.insert({2, 7, {-1, -1}});   // diagonal neighbour across zero
    index.insert({3, 7, {1, 1}});
    index.insert({4, 7, {2, 0}});     // outside interest
    index.insert({5, 7, {10, 10}});   // distant group member or occupied seat
    index.insert({6, 8, {0, 0}});     // another room, even if retained
    const auto visible = index.visible(7, {0, 0}, {1, 3, 5, 6, 99});
    CHECK((ids(visible) == std::set<std::uint64_t>{1, 2, 3, 5}));
    CHECK(visible.size() == 4U);
    CHECK(index.area_reads == 9);
    CHECK(index.account_reads == 5);
    // Moving and leaving a group replaces visibility, without a new subscription.
    CHECK((ids(index.visible(7, {10, 10}, {1})) == std::set<std::uint64_t>{1, 5}));
    CHECK(ids(index.visible(8, {0, 0}, {6})) == std::set<std::uint64_t>{6});
}

TEST_CASE("motion interest: a spread-out room delivers fewer hot rows at 2, 8 and 40 players") {
    for (const int players : {2, 8, 40}) {
        motion_index index;
        for (int i = 0; i < players; ++i) {
            // Eighteen separated areas, with up to three players in each.
            index.insert({static_cast<std::uint64_t>(i + 1), 7, {(i % 18) * 3, 0}});
        }
        std::size_t delivered = 0;
        for (int i = 0; i < players; ++i) {
            const auto visible = index.visible(7, {(i % 18) * 3, 0}, {static_cast<std::uint64_t>(i + 1)});
            delivered += visible.size();
            CHECK(index.area_reads == 9);
            CHECK(index.account_reads == 1);
        }
        const std::size_t baseline = static_cast<std::size_t>(players * players);
        CHECK(delivered < baseline);
        CHECK(delivered == (players == 40 ? 92U : static_cast<std::size_t>(players)));
    }
}

TEST_CASE("motion interest: a crowded area keeps all forty players") {
    motion_index index;
    for (std::uint64_t account = 1; account <= 40; ++account) {
        index.insert({account, 7, {0, 0}});
    }
    CHECK(index.visible(7, {0, 0}, {1}).size() == 40U);
    CHECK(index.area_reads == 9);
}

TEST_CASE("motion interest: groups and their occupied seats survive distance without treating solo players as a group") {
    struct member_row {
        std::uint64_t account_id;
        std::uint64_t room_id;
    };
    struct cart_row {
        std::uint64_t owner;
        std::uint64_t room_id;
        std::uint64_t passenger;
    };
    int group_reads = 0;
    const auto group_members = [&](const std::uint64_t group) {
        ++group_reads;
        CHECK(group == 9);
        return std::vector<member_row>{{1, 7}, {2, 7}, {3, 7}, {4, 8}};
    };
    const auto own_cart = [](const std::uint64_t account) -> std::optional<cart_row> {
        if (account == 1) { return cart_row{1, 7, 5}; }
        if (account == 2) { return cart_row{2, 7, 0}; }
        return std::nullopt;
    };
    const auto ridden_carts = [](const std::uint64_t account) {
        return account == 3 ? std::vector<cart_row>{{6, 7, 3}, {7, 8, 3}} : std::vector<cart_row>{};
    };
    CHECK((retained_motion_accounts(1, 7, 0, group_members, own_cart, ridden_carts) == std::set<std::uint64_t>{1, 5}));
    CHECK(group_reads == 0);
    CHECK((retained_motion_accounts(1, 7, 9, group_members, own_cart, ridden_carts) == std::set<std::uint64_t>{1, 2, 3, 5, 6}));
    CHECK(group_reads == 1);
    // Leaving the group releases its distant carts and avatars immediately.
    CHECK((retained_motion_accounts(1, 7, 0, group_members, own_cart, ridden_carts) == std::set<std::uint64_t>{1, 5}));
}
