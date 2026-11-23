#pragma once

// Views exercised through normal offline play, in this order or its reverse.
#include <algorithm>
#include <string>
#include <vector>

enum class benchmark_view { play, pond, sweep, walk, zoom, shot, plaza, range, bus, lamps };

struct benchmark_scenario {
    const char* name;
    benchmark_view view;
    int nearby = 0;
    bool no_water = false;
    bool moving = false;  // the crowd walks and drives (benchmark_driver::move_crowd) instead of standing
};

inline std::vector<benchmark_scenario> benchmark_scenarios(const bool hub, const bool water,
                                                          const bool control, const bool reverse) {
    using view = benchmark_view;
    std::vector<benchmark_scenario> cases;
    if (hub) {
        cases = {{"plaza", view::plaza}, {"plaza_5", view::plaza, 5}, {"plaza_40", view::plaza, 40},
                 {"plaza_40_moving", view::plaza, 40, false, true}, {"range", view::range}, {"bus", view::bus}, {"night_lamps", view::lamps}};
    } else {
        cases = {{"play", view::play}};
        if (control) { cases.push_back({"play_no_water", view::play, 0, true}); }
        cases.push_back({"play_5", view::play, 5});
        cases.push_back({"play_40", view::play, 40});
        cases.push_back({"play_40_moving", view::play, 40, false, true});
        if (water) { cases.push_back({"pond", view::pond}); }
        cases.insert(cases.end(), {{"sweep", view::sweep}, {"walk_windy_dusk", view::walk},
                                    {"zoom_flag_night", view::zoom}, {"shot_rain", view::shot}});
    }
    if (reverse) { std::reverse(cases.begin(), cases.end()); }
    return cases;
}
