#include "core/vcr_frame.h"

#include "game/mode_dispatch.h"
#include "game/sky_clock.h"
#include "game/text_ids.h"

#include <array>
#include <cstddef>
#include <cmath>
#include <string>

namespace {
constexpr std::array<const char*, 12> month_keys{
    text_camcorder_month_jan, text_camcorder_month_feb, text_camcorder_month_mar, text_camcorder_month_apr,
    text_camcorder_month_may, text_camcorder_month_jun, text_camcorder_month_jul, text_camcorder_month_aug,
    text_camcorder_month_sep, text_camcorder_month_oct, text_camcorder_month_nov, text_camcorder_month_dec};
constexpr int seconds_per_day = 24 * 60 * 60;
constexpr int hours_per_half_day = 12;
}

render_datestamp make_datestamp(const text_assets& text, const double game_day, const double utc_offset_hours) {
    const double local = game_day + utc_offset_hours / 24.0;
    // To the nearest second first, so a pinned hour shows as that hour.
    const double seconds = std::round(local * static_cast<double>(seconds_per_day));
    const double date = std::floor(seconds / static_cast<double>(seconds_per_day));
    const int minute_of_day = static_cast<int>(seconds - date * static_cast<double>(seconds_per_day)) / 60;
    const int hour = minute_of_day / 60;
    const int minute = minute_of_day % 60;
    const int twelve_hour = hour % hours_per_half_day == 0 ? hours_per_half_day : hour % hours_per_half_day;
    const civil_date day = civil_from_days(static_cast<std::int64_t>(date));
    const std::string minutes = (minute < 10 ? "0" : "") + std::to_string(minute);

    render_datestamp stamp;
    stamp.time = format_text(text, text_camcorder_datestamp_time,
                             {{"half", lookup_text(text, hour < hours_per_half_day ? text_camcorder_am : text_camcorder_pm)},
                              {"hour", std::to_string(twelve_hour)},
                              {"minute", minutes}});
    stamp.date = format_text(text, text_camcorder_datestamp_date,
                             {{"month", lookup_text(text, month_keys[static_cast<std::size_t>(day.month - 1)])},
                              {"day", std::to_string(day.day)},
                              {"year", std::to_string(day.year)}});
    return stamp;
}

render_vcr make_render_vcr(const text_assets& text, const game_state& game, const sky_controls& controls,
                           const std::int64_t local_unix_ms, const vcr_signals& signals) {
    render_vcr vcr;
    vcr.tag = signals.in_menu ? vcr_tag::play : signals.paused ? vcr_tag::pause : vcr_tag::none;
    vcr.still_frame = !signals.in_menu && signals.paused;
    vcr.show_datestamp = !signals.in_menu && signals.datestamp;
    vcr.show_offline = !signals.in_menu && !is_online(game);
    if (vcr.show_datestamp) {
        vcr.datestamp = make_datestamp(text, course_game_day(game, controls, local_unix_ms),
                                       static_cast<double>(game.course.sky.utc_offset_hours));
    }
    return vcr;
}
