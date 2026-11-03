#include "progress_rollup.h"

#include <map>
#include <tuple>

namespace {
constexpr std::int64_t micros_per_day = 86400LL * 1000000LL;

// Floor division, so times before the epoch land in the day they belong to.
std::int64_t floor_div(const std::int64_t value, const std::int64_t by) {
    const std::int64_t quotient = value / by;
    return (value % by != 0 && (value < 0) != (by < 0)) ? quotient - 1 : quotient;
}
}

std::int32_t award_month(const std::int64_t micros) {
    // Days since 1970-01-01 to a civil date (Howard Hinnant's days_from_civil, inverted).
    const std::int64_t z = floor_div(micros, micros_per_day) + 719468;
    const std::int64_t era = floor_div(z, 146097);
    const std::int64_t day_of_era = z - era * 146097;
    const std::int64_t year_of_era = (day_of_era - day_of_era / 1460 + day_of_era / 36524 - day_of_era / 146096) / 365;
    const std::int64_t day_of_year = day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
    const std::int64_t shifted_month = (5 * day_of_year + 2) / 153;  // March is 0
    const std::int64_t month = shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
    const std::int64_t year = year_of_era + era * 400 + (month <= 2 ? 1 : 0);
    return static_cast<std::int32_t>(year * 100 + month);
}

std::string rollup_key(const std::uint64_t golfer_id, const std::string& skill_id, const std::int32_t month) {
    return std::to_string(golfer_id) + ":" + skill_id + ":" + std::to_string(month);
}

std::int64_t rollup_cutoff_micros(const std::int64_t now_micros, const int keep_days) {
    return now_micros - static_cast<std::int64_t>(keep_days) * micros_per_day;
}

std::vector<rollup_sum> rolled_up(const std::vector<rolled_award>& awards) {
    std::map<std::tuple<std::uint64_t, std::string, std::int32_t>, std::int64_t> sums;
    for (const rolled_award& award : awards) {
        sums[{award.golfer_id, award.skill_id, award_month(award.at_micros)}] += award.amount;
    }
    std::vector<rollup_sum> out;
    out.reserve(sums.size());
    for (const auto& [key, amount] : sums) {
        out.push_back(rollup_sum{std::get<0>(key), std::get<1>(key), std::get<2>(key), amount});
    }
    return out;
}
