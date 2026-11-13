#include "check_runtime.h"
#include "game/mode_dispatch.h"
#include "game/server_errors.h"
#include "game/tee_times.h"
#include <algorithm>
#include <cstdio>

namespace {
// Only booked times have rows (game/tee_times.h): no row is nobody booked.
int count_at(live_check& run, const std::int64_t at) {
    for (const auto& [id, slot] : run.state(0).online.tee_slots) {
        if (slot.scheduled_at == at) { return slot.booked_count; }
    }
    return 0;
}
bool has_row(live_check& run, const std::int64_t at) {
    const auto& slots = run.state(0).online.tee_slots;
    return std::any_of(slots.begin(), slots.end(), [&](const auto& entry) { return entry.second.scheduled_at == at; });
}
}
bool check_booking(live_check& run) {
    run[0].client.join_course(run.course, false);
    if (!run.refusal(0, "join_course", error_hub_only)) { return false; }
    for (std::size_t i = 0; i < 6; ++i) { run[i].client.join_course(online_hub_course, false); }
    if (!run.require("six complete scoped hub snapshots, same uncapped world", run.until([&] {
        for (std::size_t i = 0; i < 6; ++i) {
            const auto& s = run.state(i);
            if (!s.online.room_ready || !s.online.room || !run[i].adopted_room || s.online.players.size() != 6 ||
                !s.online.bags.count(s.online.account_id) || !s.online.loadouts.count(s.online.account_id) ||
                s.online.room->room_id != run.state(0).online.room->room_id) { return false; }
        }
        return true;
    }))) { return false; }
    auto& s = run.state(0);
    // Keep the other spawn occupants within book reach but out of the way
    // of the first player's nearest F interaction (which also offers daps).
    for (std::size_t i = 1; i < 6; ++i) {
        if (!run.walk(i, s.world.hub->book.position + glm::vec3(1.6f, 0, 0))) { return false; }
    }
    if (!run.walk(0, s.world.hub->book.position + glm::vec3(-1, 0, -1)) ||
        !run.walk(0, s.world.hub->book.position)) { return false; }
    run[0].input.interact = true; run.tick(); run[0].input = {};
    if (!run.require("F at physical book opens its course picker", s.hub_flow.screen == hub_screen::book)) { return false; }
    s.hub_flow.screen = hub_screen::none;
    if (!run.open(0)) { return false; }
    // Leave enough real time for social/warmup checks, without changing the clock.
    for (const auto at : tee_times(s.online.server_now, s.tuning.server)) {
        if (at > s.online.server_now + 120000000) { run.due = at; break; }
    }
    if (!run.require("natural quarter-hour available on empty database, no slot rows",
                     run.due != 0 && s.online.tee_slots.empty())) { return false; }
    std::printf("UTC due microseconds: %lld\n", static_cast<long long>(run.due));
    const auto interval = s.tuning.server.tee_interval_minutes * 60LL * 1000000LL;
    if (!run.book(0, run.due + interval)) { return false; }
    const auto stale = s.online.reservation->reservation_id;
    if (!run.book(0, run.due)) { return false; }
    net_command cancel; cancel.type = net_command_type::cancel_tee; cancel.reservation_id = stale;
    run.send(0, cancel);
    if (!run.refusal(0, "cancel_tee", error_stale_booking) ||
        !run.require("stale cancel preserves replacement", s.online.reservation && s.online.reservation->scheduled_at == run.due)) { return false; }
    run.action(0, net_command_type::cancel_tee);
    if (!run.require("cancel removes reservation and the unheld slot row", run.until([&] { return !s.online.reservation && !has_row(run, run.due); }))) { return false; }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!run.book(i, run.due)) { return false; }
    }
    if (!run.require("five reservations share slot/group, listing count five", run.until([&] {
        if (count_at(run, run.due) != 5) { return false; }
        for (std::size_t i = 0; i < 5; ++i) {
            if (run.state(i).online.reservation->slot_id != s.online.reservation->slot_id) { return false; }
        }
        return true;
    }))) { return false; }
    if (!run.book(5, run.due + 2 * interval)) { return false; }
    const auto previous = run.state(5).online.reservation->reservation_id;
    net_command full; full.type = net_command_type::book_tee; full.course_id = run.course;
    full.scheduled_at = run.due; full.reservation_id = previous; run.send(5, full);
    if (!run.refusal(5, "book_tee", error_group_full) || !run.require("full replacement keeps complete prior booking",
        run.state(5).online.reservation && run.state(5).online.reservation->reservation_id == previous &&
        run.state(5).online.reservation->scheduled_at == run.due + 2 * interval)) { return false; }
    if (!run.walk(5, run.state(5).area.bus->position)) { return false; }
    run.action(5, net_command_type::take_bus);
    if (!run.refusal(5, "take_bus", error_bus_too_early)) { return false; }
    return run.require("refused early bus retains complete hub", run.state(5).online.room_ready && run.state(5).online.room->course_id == online_hub_course);
}
