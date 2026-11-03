#pragma once

// Old XP awards summed per golfer, skill and calendar month (UTC), so the
// award ledger keeps per-award detail for server.xp_award_keep_days only.
// The sums keep every point: a golfer's skill XP is its awards plus its
// rollups. Run by the module's nightly roll_up_xp. Pure.

#include <cstdint>
#include <string>
#include <vector>

struct rolled_award {
    std::uint64_t golfer_id = 0;
    std::string skill_id;
    std::int64_t amount = 0;
    std::int64_t at_micros = 0;  // since the Unix epoch
};

struct rollup_sum {
    std::uint64_t golfer_id = 0;
    std::string skill_id;
    std::int32_t month = 0;  // award_month
    std::int64_t amount = 0;
};

// The UTC calendar month `micros` since the Unix epoch falls in, as yyyymm.
std::int32_t award_month(std::int64_t micros);

// The unique key of a golfer's rollup of a skill in a month.
std::string rollup_key(std::uint64_t golfer_id, const std::string& skill_id, std::int32_t month);

// Awards before this are rolled up: `keep_days` before `now_micros`.
std::int64_t rollup_cutoff_micros(std::int64_t now_micros, int keep_days);

// `awards` summed by golfer, skill and month, in that order.
std::vector<rollup_sum> rolled_up(const std::vector<rolled_award>& awards);
