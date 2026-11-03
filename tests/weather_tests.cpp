#include "doctest.h"

#include "game/weather.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <optional>
#include <cstdint>
#include <vector>

namespace {
// The shipped course with this id.
const course_definition& course_named(const std::string& id) {
    for (const course_definition& course : shipped_content().courses) {
        if (course.id == id) {
            return course;
        }
    }
    CHECK(id == "no such course");
    return shipped_content().courses.front();
}

// The shipped tuning's weather on a course, every 15 game minutes over 60
// game days.
std::vector<weather_state> weather_over_days(const course_definition& course) {
    const game_tuning& tuning = shipped_content().tuning;
    const double real_ms_per_game_hour = 3600000.0 / tuning.sky_clock.game_days_per_real_day;
    const std::int64_t step = static_cast<std::int64_t>(real_ms_per_game_hour / 4.0);
    std::vector<weather_state> out;
    for (std::int64_t i = 0; i < 60 * 24 * 4; ++i) {
        out.push_back(weather_at(tuning.weather, tuning.wind, tuning.sky_clock, course, tuning.sky_clock.anchor_unix_ms + i * step));
    }
    return out;
}

bool foggy(const weather_kind kind) {
    return kind == weather_kind::light_fog || kind == weather_kind::fog;
}
}

TEST_CASE("the weather is the same at the same moment on the same course, and differs between courses") {
    const game_tuning& tuning = shipped_content().tuning;
    const course_definition& here = course_named("marienlyst_golfklub");
    const std::int64_t now = 1800000000000;
    const weather_state a = weather_at(tuning.weather, tuning.wind, tuning.sky_clock, here, now);
    const weather_state b = weather_at(tuning.weather, tuning.wind, tuning.sky_clock, here, now);
    CHECK(a.kind == b.kind);
    CHECK(a.wind == b.wind);
    CHECK(a.wetness == b.wetness);
    CHECK(course_weather_seed("marienlyst_golfklub") != course_weather_seed("kalo_golf_club"));
    const weather_state there = weather_at(tuning.weather, tuning.wind, tuning.sky_clock, course_named("kalo_golf_club"), now);
    CHECK(there.wind != a.wind);
}

TEST_CASE("every kind of weather comes round, mostly dry") {
    const std::vector<weather_state> days = weather_over_days(course_named("marienlyst_golfklub"));
    std::array<int, weather_kind_count> counts{};
    for (const weather_state& weather : days) {
        ++counts[static_cast<std::size_t>(weather.kind)];
    }
    for (const int count : counts) {
        CHECK(count > 0);
    }
    const int dry = counts[static_cast<std::size_t>(weather_kind::clear)] + counts[static_cast<std::size_t>(weather_kind::cloudy)] +
        counts[static_cast<std::size_t>(weather_kind::light_fog)] + counts[static_cast<std::size_t>(weather_kind::fog)];
    CHECK(static_cast<float>(dry) / static_cast<float>(days.size()) > 0.6f);
}

TEST_CASE("the weather's numbers move smoothly and push each other") {
    const weather_tuning& tuning = shipped_content().tuning.weather;
    for (const char* const course : {"marienlyst_golfklub", "kalo_golf_club"}) {
        const std::vector<weather_state> days = weather_over_days(course_named(course));
        int sun_showers = 0;
        for (std::size_t i = 0; i < days.size(); ++i) {
            const weather_state& now = days[i];
            for (const float amount : {now.cloud, now.rain, now.fog, now.rainbow, now.wetness}) {
                CHECK(amount >= 0.0f);
                CHECK(amount <= 1.0f);
            }
            // Rain falls in full only under cloud; a clear sky thins it.
            CHECK(now.rain <= tuning.sun_shower_keep + (1.0f - tuning.sun_shower_keep) * now.cloud + 0.0001f);
            // Rain clears fog; a rainbow needs rain and sun.
            if (now.fog > 0.0f) {
                CHECK(now.rain < tuning.fog_rain_clear);
            }
            if (now.rainbow > 0.0f) {
                CHECK(now.rain > 0.0f);
                CHECK(now.cloud < tuning.sun_shower_cloud);
            }
            sun_showers += now.kind == weather_kind::sun_shower ? 1 : 0;
            // Already as the weather allows it: coupling it changes nothing.
            const weather_state again = coupled_weather(now, tuning);
            CHECK(near(again.rain, now.rain, 0.0001f));
            CHECK(near(again.thunder, now.thunder, 0.0001f));
            CHECK(near(again.fog, now.fog, 0.0001f));
            CHECK(again.kind == now.kind);
            CHECK(now.thunder <= now.cloud);
            if (i > 0) {
                const weather_state& before = days[i - 1];
                // Nothing snaps: a quarter at most in 15 game minutes.
                CHECK(std::abs(now.cloud - before.cloud) < 0.25f);
                CHECK(std::abs(now.rain - before.rain) < 0.25f);
                CHECK(std::abs(now.fog - before.fog) < 0.25f);
            }
        }
        CHECK(sun_showers > 0);
    }
}

TEST_CASE("there is always some wind, sometimes strong") {
    const game_tuning& tuning = shipped_content().tuning;
    float strongest = 0.0f;
    for (const weather_state& weather : weather_over_days(course_named("kalo_golf_club"))) {
        CHECK(wind_speed(weather) >= tuning.wind.calm_speed - 0.001f);
        strongest = std::max(strongest, wind_speed(weather));
    }
    CHECK(strongest > 0.6f * tuning.wind.max_speed);
}

TEST_CASE("the ground gets wetter in steady rain and dries in the sun") {
    const std::vector<weather_state> days = weather_over_days(course_named("mollerup_golf_club"));
    const weather_tuning& tuning = shipped_content().tuning.weather;
    float wettest = 0.0f;
    int wetting = 0;
    int drying = 0;
    for (std::size_t i = 1; i < days.size(); ++i) {
        const weather_state& now = days[i];
        const weather_state& before = days[i - 1];
        wettest = std::max(wettest, now.wetness);
        if (now.rain >= tuning.light_rain && before.rain >= tuning.light_rain && now.cloud >= 1.0f && before.cloud >= 1.0f) {
            CHECK(now.wetness >= before.wetness);
            ++wetting;
        }
        if (now.rain == 0.0f && before.rain == 0.0f && now.cloud == 0.0f && before.cloud == 0.0f) {
            CHECK(now.wetness <= before.wetness);
            ++drying;
        }
    }
    CHECK(wetting > 0);
    CHECK(drying > 0);
    CHECK(wettest > 0.5f);
}

TEST_CASE("every kind's preset reads as that kind") {
    const weather_tuning& tuning = shipped_content().tuning.weather;
    weather_state calm;
    calm.wind = glm::vec3(1.0f, 0.0f, 0.0f);
    calm.wetness = 0.4f;
    for (std::size_t i = 0; i < weather_kind_count; ++i) {
        const weather_kind kind = static_cast<weather_kind>(i);
        const weather_state preset = with_weather_kind(calm, kind, tuning);
        CHECK(preset.kind == kind);
        CHECK(weather_kind_of(preset, tuning) == kind);
        CHECK(preset.wind == calm.wind);
        CHECK(preset.wetness == calm.wetness);
    }
    CHECK(with_weather_kind(calm, weather_kind::heavy_rain, tuning).rain == tuning.heavy_rain);
    CHECK(with_weather_kind(calm, weather_kind::sun_shower, tuning).rainbow > 0.5f);
    CHECK(with_weather_kind(calm, weather_kind::light_rain, tuning).rainbow == 0.0f);
    const weather_state fog = with_weather_kind(calm, weather_kind::fog, tuning);
    CHECK(fog.fog == tuning.fog);
    CHECK(with_weather_kind(fog, weather_kind::clear, tuning).fog == 0.0f);

    // An amount is called the preset it is nearest.
    CHECK(rain_kind(tuning, 0.5f * tuning.light_rain - 0.01f) == weather_kind::clear);
    CHECK(rain_kind(tuning, tuning.light_rain) == weather_kind::light_rain);
    CHECK(rain_kind(tuning, tuning.rain) == weather_kind::rain);
    CHECK(rain_kind(tuning, tuning.heavy_rain) == weather_kind::heavy_rain);
    CHECK(fog_kind(tuning, tuning.light_fog) == weather_kind::light_fog);
}

TEST_CASE("a round of 18 holes mostly sees at most three skies, and fog lies most of the day") {
    const game_tuning& tuning = shipped_content().tuning;
    // Two real hours, in the 15 game minutes weather_over_days samples.
    constexpr double round_real_hours = 2.0;
    const std::size_t round_samples =
        static_cast<std::size_t>(round_real_hours * tuning.sky_clock.game_days_per_real_day * 4.0);
    for (const char* const course : {"marienlyst_golfklub", "kalo_golf_club", "mollerup_golf_club"}) {
        const std::vector<weather_state> days = weather_over_days(course_named(course));
        int rounds = 0;
        int calm_rounds = 0;
        for (std::size_t start = 0; start + round_samples <= days.size(); start += round_samples / 2) {
            std::array<bool, weather_kind_count> seen{};
            int kinds = 0;
            for (std::size_t i = start; i < start + round_samples; ++i) {
                bool& was_seen = seen[static_cast<std::size_t>(days[i].kind)];
                kinds += was_seen ? 0 : 1;
                was_seen = true;
            }
            ++rounds;
            calm_rounds += kinds <= 3 ? 1 : 0;
        }
        // A storm building through the rains can show a fourth.
        CHECK(static_cast<float>(calm_rounds) / static_cast<float>(rounds) > 0.8f);

        // Fog, once it lies, lasts over 8 game hours on average (thin or thick).
        int fog_spells = 0;
        std::size_t fog_samples = 0;
        for (std::size_t i = 0; i < days.size(); ++i) {
            if (foggy(days[i].kind)) {
                ++fog_samples;
                fog_spells += i == 0 || !foggy(days[i - 1].kind) ? 1 : 0;
            }
        }
        if (fog_spells > 0) {
            CHECK(static_cast<float>(fog_samples) / static_cast<float>(fog_spells) > 4.0f * 8.0f);
        }
    }
}

TEST_CASE("each kind of weather comes about as often as it should") {
    const game_tuning& tuning = shipped_content().tuning;
    const double ms_per_hour = 3600000.0 / tuning.sky_clock.game_days_per_real_day;
    constexpr int days = 140;
    const char* const courses[] = {"marienlyst_golfklub", "kalo_golf_club", "mollerup_golf_club", "horsens_golfklub",
                                   "himmerland_new_course"};
    std::array<int, weather_kind_count> spells{};
    std::array<int, weather_kind_count> hours{};
    int fog_spells = 0;
    int fog_hours = 0;
    int total_hours = 0;
    // Runs of days with an hour's rain or more, and of days without.
    std::vector<int> wet_runs;
    std::vector<int> dry_runs;
    for (const char* const course : courses) {
        int previous = -1;
        bool was_foggy = false;
        int rainy_hours = 0;
        std::optional<bool> run_wet;
        int run = 0;
        for (int hour = 0; hour < days * 24; ++hour) {
            const weather_state weather =
                weather_at(tuning.weather, tuning.wind, tuning.sky_clock, course_named(course),
                           tuning.sky_clock.anchor_unix_ms + static_cast<std::int64_t>(hour * ms_per_hour));
            const int kind = static_cast<int>(weather.kind);
            spells[static_cast<std::size_t>(kind)] += kind != previous ? 1 : 0;
            ++hours[static_cast<std::size_t>(kind)];
            previous = kind;
            const bool is_foggy = foggy(weather.kind);
            fog_spells += is_foggy && !was_foggy ? 1 : 0;
            fog_hours += is_foggy ? 1 : 0;
            was_foggy = is_foggy;
            ++total_hours;
            rainy_hours += rain_kind(tuning.weather, weather.rain) != weather_kind::clear ? 1 : 0;
            if (hour % 24 == 23) {
                const bool wet = rainy_hours > 0;
                if (run_wet && *run_wet == wet) {
                    ++run;
                } else {
                    if (run_wet) {
                        (*run_wet ? wet_runs : dry_runs).push_back(run);
                    }
                    run_wet = wet;
                    run = 1;
                }
                rainy_hours = 0;
            }
        }
    }
    const double course_days = static_cast<double>(days) * 5.0;
    const auto every_days = [&](const weather_kind kind) {
        const int count = spells[static_cast<std::size_t>(kind)];
        return count > 0 ? course_days / count : course_days * 2.0;
    };
    const auto share = [&](const weather_kind kind) {
        return static_cast<double>(hours[static_cast<std::size_t>(kind)]) / total_hours;
    };
    // Mostly dry; rain every few days, a downpour about weekly, thunder a
    // couple of times a month.
    CHECK(share(weather_kind::clear) + share(weather_kind::cloudy) > 0.7);
    CHECK(every_days(weather_kind::rain) > 2.5);
    CHECK(every_days(weather_kind::rain) < 7.0);
    CHECK(every_days(weather_kind::heavy_rain) > 5.0);
    CHECK(every_days(weather_kind::heavy_rain) < 14.0);
    CHECK(every_days(weather_kind::thunderstorm) > 12.0);
    CHECK(every_days(weather_kind::thunderstorm) < 35.0);
    // Weather comes in spells: days of rain on and off, days of sun.
    const auto mean = [](const std::vector<int>& runs) {
        double sum = 0.0;
        for (const int run : runs) {
            sum += run;
        }
        return runs.empty() ? 0.0 : sum / static_cast<double>(runs.size());
    };
    CHECK(mean(wet_runs) > 2.5);
    CHECK(*std::max_element(wet_runs.begin(), wet_runs.end()) >= 7);
    CHECK(mean(dry_runs) > 3.0);
    CHECK(*std::max_element(dry_runs.begin(), dry_runs.end()) >= 10);
    // A sun shower about weekly, as a storm clears.
    CHECK(every_days(weather_kind::sun_shower) > 4.0);
    CHECK(every_days(weather_kind::sun_shower) < 12.0);
    // Fog about once a fortnight, and then most of the day or longer.
    REQUIRE(fog_spells > 0);
    CHECK(course_days / fog_spells > 9.0);
    CHECK(course_days / fog_spells < 25.0);
    CHECK(static_cast<double>(fog_hours) / fog_spells > 10.0);
}
