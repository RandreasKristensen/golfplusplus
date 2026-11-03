#include "doctest.h"

#include "progress_rollup.h"
#include "test_support.h"

#include <cstdint>
#include <vector>

namespace {
constexpr std::int64_t micros_per_day = 86400LL * 1000000LL;

// Midnight UTC starting a day, days since the epoch.
std::int64_t day(const std::int64_t days) {
    return days * micros_per_day;
}
}

TEST_CASE("an award's month is its UTC calendar month, either side of every boundary") {
    CHECK(award_month(0) == 197001);
    CHECK(award_month(day(31) - 1) == 197001);
    CHECK(award_month(day(31)) == 197002);
    // 2024 is a leap year: 29 February, then 1 March (days 19782 and 19783).
    CHECK(award_month(day(19782)) == 202402);
    CHECK(award_month(day(19783) - 1) == 202402);
    CHECK(award_month(day(19783)) == 202403);
    // New Year 2026 (day 20454), and 31 December 2025 just before.
    CHECK(award_month(day(20454) - 1) == 202512);
    CHECK(award_month(day(20454)) == 202601);
    // 2000 is a leap year, 2100 is not.
    CHECK(award_month(day(11016)) == 200002);  // 29 February 2000
    CHECK(award_month(day(47541)) == 210003);  // 1 March 2100, the day after 28 February
    CHECK(award_month(day(47540)) == 210002);
    CHECK(award_month(-1) == 196912);
}

TEST_CASE("awards older than the kept days are the ones before the cutoff") {
    const std::int64_t now = day(20454) + 12345;
    CHECK(rollup_cutoff_micros(now, 90) == now - day(90));
    CHECK(shipped_content().tuning.server.xp_award_keep_days == 90);
    CHECK(shipped_content().tuning.server.xp_rollup_interval_hours > 0.0f);
}

TEST_CASE("rolled up awards sum per golfer, skill and month, keeping every point") {
    const std::int64_t january = day(20454);       // 1 January 2026
    const std::int64_t february = day(20454 + 31);  // 1 February 2026
    const std::vector<rolled_award> awards{
        {1, "putting", 10, january},
        {1, "putting", -3, january + day(5)},  // a correction
        {1, "putting", 7, february - 1},
        {1, "putting", 4, february},
        {1, "driving", 2, january},
        {2, "putting", 5, january},
    };
    const std::vector<rollup_sum> sums = rolled_up(awards);
    REQUIRE(sums.size() == 4U);
    CHECK(sums[0].golfer_id == 1U);
    CHECK(sums[0].skill_id == "driving");
    CHECK(sums[0].month == 202601);
    CHECK(sums[0].amount == 2);
    CHECK(sums[1].skill_id == "putting");
    CHECK(sums[1].month == 202601);
    CHECK(sums[1].amount == 14);
    CHECK(sums[2].skill_id == "putting");
    CHECK(sums[2].month == 202602);
    CHECK(sums[2].amount == 4);
    CHECK(sums[3].golfer_id == 2U);
    CHECK(sums[3].amount == 5);
    std::int64_t awarded = 0;
    for (const rolled_award& award : awards) {
        awarded += award.amount;
    }
    std::int64_t summed = 0;
    for (const rollup_sum& sum : sums) {
        summed += sum.amount;
    }
    CHECK(summed == awarded);
    CHECK(rollup_key(1, "putting", 202601) == "1:putting:202601");
    CHECK(rollup_key(1, "putting", 202601) != rollup_key(11, "putting", 202601));
    CHECK(rolled_up({}).empty());
}
