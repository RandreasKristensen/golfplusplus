// Measured, not asserted: what the tee book's requests cost through the real SDK
// and the module. Client time is from dispatching a command to the row that
// answers it; the module's own time is read from the server's metrics
// (the reducer's wasm run time and the fuel it used), so a cold destination
// build shows apart from a warm request. Bounded: a few dozen requests.
#include "check_runtime.h"
#include "game/mode_dispatch.h"
#include "game/tee_times.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace {
using clock_type = std::chrono::steady_clock;

struct module_cost {
    double wasm_ms = 0.0;
    double query_ms = 0.0;  // the reducer plus its subscription queries
    double fuel = 0.0;
    double calls = 0.0;
};
using module_costs = std::map<std::string, module_cost>;

double milliseconds_since(const clock_type::time_point start) {
    return std::chrono::duration<double, std::milli>(clock_type::now() - start).count();
}

// The reducer's cumulative counters on the server, by reducer name.
module_costs read_costs(const std::string& server) {
    module_costs costs;
    std::string text;
    if (FILE* pipe = popen(("curl.exe -s " + server + "/v1/metrics").c_str(), "r")) {
        char buffer[4096];
        while (std::fgets(buffer, sizeof buffer, pipe) != nullptr) { text += buffer; }
        pclose(pipe);
    }
    const auto field = [](const std::string& line, const std::string& name, const bool as_sum) {
        const std::string head = name + (as_sum ? "_sum{" : "{");
        if (line.rfind(head, 0) != 0) { return std::pair<std::string, double>{}; }
        const auto key = line.find("reducer=\"");
        const auto end = key == std::string::npos ? std::string::npos : line.find('"', key + 9);
        const auto value = line.rfind(' ');
        if (end == std::string::npos || value == std::string::npos) { return std::pair<std::string, double>{}; }
        return std::pair<std::string, double>{line.substr(key + 9, end - key - 9), std::atof(line.c_str() + value + 1)};
    };
    for (std::size_t from = 0; from < text.size();) {
        const auto to = text.find('\n', from);
        const std::string line = text.substr(from, to == std::string::npos ? std::string::npos : to - from);
        from = to == std::string::npos ? text.size() : to + 1;
        if (const auto [name, value] = field(line, "reducer_wasm_time_usec", false); !name.empty()) { costs[name].wasm_ms = value / 1000.0; }
        if (const auto [name, value] = field(line, "reducer_wasmtime_fuel_used", false); !name.empty()) { costs[name].fuel = value; }
        if (const auto [name, value] = field(line, "spacetime_reducer_plus_query_duration_sec", true); !name.empty()) { costs[name].query_ms = value * 1000.0; }
        if (const auto [name, value] = field(line, "spacetime_reducer_plus_query_duration_sec_count", false); !name.empty()) { costs[name].calls = value; }
    }
    return costs;
}

void report(const char* label, const double client_ms, const module_costs& before, const module_costs& after, const char* reducer) {
    const module_cost& a = after.count(reducer) ? after.at(reducer) : module_cost{};
    const module_cost b = before.count(reducer) ? before.at(reducer) : module_cost{};
    std::printf("MEASURE %-46s client %8.1f ms | %s: %6.0f call, wasm %8.1f ms, with queries %8.1f ms, fuel %.0f\n", label, client_ms, reducer,
                a.calls - b.calls, a.wasm_ms - b.wasm_ms, a.query_ms - b.query_ms, a.fuel - b.fuel);
}

// The answer to the latest list request: its numbered status row, with the scoped counts published.
bool answered(const online_view& online, const std::string& course, const std::uint64_t answers_before) {
    return online.book_answers > answers_before && online.book_ready && online.book_course_id == course && online.server_now > 0;
}

// Dispatch to answer. Pacing is 1 ms so a warm request is not rounded to a frame.
bool timed_open(live_check& run, const std::string& course, const char* label) {
    const module_costs before = read_costs(run.server);
    const std::uint64_t answers_before = run.state(0).online.book_answers;
    const auto start = clock_type::now();
    net_command command; command.type = net_command_type::open_tee_book; command.course_id = course;
    run.send(0, command);
    bool done = false;
    while (!done && milliseconds_since(start) < 120000.0) { run.tick(std::chrono::milliseconds(1)); done = answered(run.state(0).online, course, answers_before); }
    const double client_ms = milliseconds_since(start);
    report(label, client_ms, before, read_costs(run.server), "open_tee_book");
    return run.require(label, done);
}
}

bool measure_tee_book(live_check& run) {
    run[0].client.join_course(online_hub_course);
    if (!run.require("hub snapshot", run.until([&] { const auto& s = run.state(0); return s.online.room_ready && s.online.room && run[0].adopted_room; }))) { return false; }
    auto& s = run.state(0);
    if (!run.walk(0, s.world.hub->book.position + glm::vec3(-1, 0, -1)) || !run.walk(0, s.world.hub->book.position)) { return false; }
    run.tick();
    const std::string large = "hedensted_golf_klub", other_large = "jelling_golfklub", small = "jelling_par_3", travel_to = "kalo_golf_club";
    std::printf("MEASURE note: a never-opened course is cold in this module instance; the others are warm\n");
    if (!timed_open(run, large, "first open, large course (cold)")) { return false; }
    if (!timed_open(run, large, "second open, same course")) { return false; }
    for (int i = 1; i <= 5; ++i) {
        const std::string label = "manual refresh " + std::to_string(i);
        if (!timed_open(run, large, label.c_str())) { return false; }
    }
    if (!timed_open(run, other_large, "another large course (cold)")) { return false; }
    if (!timed_open(run, small, "another small course (cold)")) { return false; }
    if (!timed_open(run, large, "back to the first course")) { return false; }
    run.wait(61.0f);
    if (!timed_open(run, large, "periodic refresh after 61 s idle")) { return false; }
    // Cold travel: open, book the next time, take the bus, for a course nothing has touched.
    if (!timed_open(run, travel_to, "open the travel course (cold)")) { return false; }
    const hub_clock clock{s.tuning.sky_clock, static_cast<double>(s.hub_sky.utc_offset_hours)};
    const auto times = tee_times(s.online.server_now, s.tuning.server, clock);
    if (!run.require("a bookable time exists", !times.empty())) { return false; }
    run.course = travel_to;
    module_costs before = read_costs(run.server);
    auto start = clock_type::now();
    net_command book; book.type = net_command_type::book_tee; book.course_id = travel_to; book.scheduled_at = times.front(); book.takes_cart = false;
    book.reservation_id = 0; run.send(0, book);
    bool done = false;
    while (!done && milliseconds_since(start) < 120000.0) { run.tick(std::chrono::milliseconds(1)); const auto& r = s.online.reservation; done = r && r->scheduled_at == times.front(); }
    report("book the first time (travel course)", milliseconds_since(start), before, read_costs(run.server), "book_tee");
    if (!run.require("booking row", done)) { return false; }
    if (!run.walk(0, s.area.bus->position)) { return false; }
    before = read_costs(run.server);
    start = clock_type::now();
    const bool arrived = run.travel(0, travel_to);
    report("take the bus, cold destination", milliseconds_since(start), before, read_costs(run.server), "take_bus");
    return arrived;
}
