#include "check_runtime.h"
#include "game/boarding.h"
#include "game/tee_times.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/golf_bag.h"
#include "game/hub_scene.h"
#include "game/progression.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "game/scorecard.h"
#include "game/shot_snapshot.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
namespace {
constexpr std::int64_t micros = 1000000;
const room_shot* last_shot(live_check& run, const std::size_t peer) {
    const auto& shots = run[peer].shots;
    const auto mine = std::find_if(shots.rbegin(), shots.rend(), [&](const auto& shot) { return shot.account_id == run.state(peer).online.account_id; });
    return mine == shots.rend() ? nullptr : &*mine;
}
bool in_snapshot(const room_shot& shot, const glm::vec3 ball) {
    return std::any_of(shot.record.snapshot.begin(), shot.record.snapshot.end(), [&](const auto& e) { return e.kind == snapshot_kind::ball && glm::length(e.position - ball) < 0.01f; });
}
bool strike(live_check& run, const std::size_t peer, const bool reject_iron = false, const float backswing = 0.2f) {
    auto& s = run.state(peer);
    const auto& ball = s.online.balls.at(s.online.account_id);
    const auto& bag = s.online.bags.at(s.online.account_id);
    const auto club = std::find_if(s.clubs.begin(), s.clubs.end(), [&](const auto& c) { return c.id == bag.club_in_hand; });
    if (!run.require("server supplied club exists", club != s.clubs.end())) { return false; }
    net_command c; c.type = net_command_type::take_shot; c.stroke = ball.stroke_count + 1;
    c.shot = stock_setup(*club, lie_at(s.area, ball.position), ball.position,
        yaw_towards(ball.position, s.holes.at(static_cast<std::size_t>(ball.hole)).pin_position), handedness::right, 0, s.tuning.setup);
    c.shot.backswing = backswing; c.shot.shot_at = shot_unix_ms(s);
    c.flags_out = s.online.flags_out;
    if (reject_iron) {
        c.shot.club_id = s.clubs.front().id; run.send(peer, c);
        return run.refusal(peer, reducer_take_shot, error_not_available);
    }
    const auto count = run[peer].shots.size(); run.send(peer, c);
    if (!run.require("actual server shot accepted", run.until([&] {
        const auto& shots = run[peer].shots;
        return std::any_of(shots.begin() + static_cast<std::ptrdiff_t>(count), shots.end(), [&](const auto& shot) { return shot.account_id == s.online.account_id; });
    }))) { return false; }
    const auto& shot = *last_shot(run, peer);
    const auto simulated = room_shot_result(s, shot);
    return run.require("live WASM shot rest agrees with native replay", glm::length(shot.rest - simulated.rest_position) < 0.001f);
}
// Real time only: waits for the server clock, connected, bounded by how far off it was.
bool wait_for_clock(live_check& run, const std::size_t peer, const std::int64_t at, const char* label) {
    const auto& s = run.state(peer);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60 + std::max<std::int64_t>(0, at - s.online.server_now) / micros);
    while (s.online.server_now < at) {
        if (s.online.status != net_status::connected || s.online.server_now <= 0 || std::chrono::steady_clock::now() >= deadline) {
            return run.require(label, false);
        }
        std::printf("waiting for %s: %.0f seconds\n", label, static_cast<double>(at - s.online.server_now) / micros);
        run.wait(std::min(30.0f, static_cast<float>(at - s.online.server_now) / micros + 0.05f));
    }
    return run.require(label, true);
}
bool warmup(live_check& run) {
    auto& s = run.state(0);
    const auto before = save_data_to_json(s.online.progress);
    const auto station = warmup_position(s.area, 0, s.tuning);
    if (!run.walk(0, station, 90)) { return false; }
    request_ball_preparation(s, net_command_type::tee_up);
    if (!run.require("green warmup supplies authoritative putter and unscored ball", run.until([&] {
        return s.ball && s.ball->kind == ball_kind::warmup && !s.hub_flow.ball_request && s.online.bags.at(s.online.account_id).club_in_hand == s.clubs.back().id;
    }))) { return false; }
    move_bag(s);
    if (!run.require("bag set down through accepted handling", run.until([&] { return !s.handling && s.online.bags.at(s.online.account_id).place.where == bag_where::on_ground; }))) { return false; }
    if (!strike(run, 0, true) || !strike(run, 0)) { return false; }
    run.wait(4);
    if (!run.require("warmup awards no XP/score/completion/start", save_data_to_json(s.online.progress) == before && s.online.reservation->started_at == 0 &&
        std::all_of(my_room_player(s)->round_strokes.begin(), my_room_player(s)->round_strokes.end(), [](int score) { return score == 0; }))) { return false; }
    return run.walk(0, s.world.hole_starts.front().position, 90);
}
void print_start(live_check& run, const std::size_t peer) {
    const auto& o = run.state(peer).online;
    std::printf("start audit expected: reservation_id=%llu account_id=%llu scheduled_at=%lld started_at=%lld lateness=%lld\n",
                static_cast<unsigned long long>(o.reservation->reservation_id), static_cast<unsigned long long>(o.account_id),
                static_cast<long long>(o.reservation->scheduled_at), static_cast<long long>(o.reservation->started_at),
                static_cast<long long>(o.reservation->started_at - o.reservation->scheduled_at));
}
}
bool check_round(live_check& run) {
    auto& s = run.state(0);
    auto& blocker = run.state(5);
    const auto& server = s.tuning.server;
    const auto earlier = run.due - server.tee_interval_minutes * minute_micros;
    const auto bus_early = server.bus_early_minutes * minute_micros;
    // A genuine earlier group: a golfer booked the time before, boarded and not started.
    if (!run.walk(5, blocker.world.hub->book.position) || !run.open(5) ||
        !run.require("precondition: earlier group's time still bookable", blocker.online.server_now + minute_micros < earlier) ||
        !run.book(5, earlier)) { return false; }
    if (!wait_for_clock(run, 5, earlier - bus_early, "earlier group's bus window opens on the server clock") ||
        !run.walk(5, blocker.area.bus->position) || !run.travel(5, run.course)) { return false; }
    // Nothing was heard of this group's boarding yet: the PA calls when its window opens, from the hub.
    for (std::size_t i = 0; i < 5; ++i) { run.state(i).audio_events.clear(); }
    if (!wait_for_clock(run, 0, run.due - bus_early, "group's bus window opens on the server clock")) { return false; }
    if (!run.require("the starter's PA calls the group's boarding and the bus doors open", run.until([&] {
        const auto& heard = run.state(2).audio_events;
        return std::any_of(heard.begin(), heard.end(), [](const audio_event& event) { return event.type == audio_event_type::boarding_call; }) && bus_doors_open(run.state(2));
    }))) { return false; }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!run.walk(i, run.state(i).area.bus->position) || !run.travel(i, run.course)) { return false; }
        if (i == 0 && !run.require("travel removes paired row for remaining hub observer", run.until([&] { return run.state(2).online.daps.empty(); }))) { return false; }
    }
    // What each chose with the booking is what the round starts with: a cart of one's own, or the bag on one's back.
    for (std::size_t i = 0; i < 5; ++i) {
        const auto& o = run.state(i).online;
        const bool cart = (i == 3 || i == 4) && run.course_has_carts();
        const auto bag = o.bags.find(o.account_id);
        if (!run.require("the booking's cart or carry is what the round starts with", (o.carts.count(o.account_id) != 0) == cart && bag != o.bags.end() &&
            bag->second.place.where == (cart ? bag_where::on_cart : bag_where::on_back))) { return false; }
    }
    const auto slot = s.online.reservation->slot_id;
    if (!run.require("one course world, group of five, fresh cards contain only unplayed holes", run.until([&] {
        for (std::size_t i = 0; i < 5; ++i) {
            const auto& o = run.state(i).online;
            if (!o.room || o.room->room_id != s.online.room->room_id || o.group_sizes.count(slot) == 0 || o.group_sizes.at(slot) != 5) { return false; }
            const auto card = build_scorecard_data(run.state(i), string_table{});
            if (card.players.size() != 5) { return false; }
            for (const auto& p : card.players) {
                if (p.name.empty() || p.total_strokes != 0) { return false; }
                for (const auto strokes : p.strokes) { if (strokes != 0) { return false; } }
            }
        }
        return true;
    }))) { return false; }
    // Early with the earlier group still waiting: refused, before any long walking.
    if (!run.require("precondition: earlier group boarded unstarted in this world, group's time well ahead", run.until([&] {
        const auto& r = blocker.online.reservation;
        return r && r->scheduled_at == earlier && r->started_at == 0 && blocker.online.room && blocker.online.room->room_id == s.online.room->room_id;
    }) && s.online.server_now + 5 * minute_micros < run.due)) { return false; }
    run.action(0, net_command_type::start_booked_round);
    if (!run.refusal(0, "start_booked_round", error_tee_too_early) ||
        !run.require("refused early call prepares nothing", !s.online.balls.count(s.online.account_id) && !my_room_player(s)->round_started)) { return false; }
    run.action(5, net_command_type::leave_group);
    if (!run.require("earlier group's golfer gives up their time and stays in the world", run.until([&] {
        const auto me = my_room_player(blocker); return !blocker.online.reservation && me && me->group_id == 0;
    }))) { return false; }
    if (!warmup(run)) { return false; }
    if (!run.require("precondition: clear tee before the group's time", s.online.server_now + minute_micros < run.due)) { return false; }
    for (std::size_t i = 0; i < 5; ++i) { request_ball_preparation(run.state(i), net_command_type::start_booked_round); }
    if (!run.require("clear early call: five first tee balls prepared in separate lanes without starting audit", run.until([&] {
        for (std::size_t i = 0; i < 5; ++i) {
            const auto& each = run.state(i);
            const auto member = my_room_player(each);
            if (!each.ball || each.ball->kind == ball_kind::warmup || each.hub_flow.ball_request || !member || !member->round_started ||
                !each.online.reservation || each.online.reservation->started_at != 0) { return false; }
        }
        for (const auto& [id, ball] : s.online.balls) {
            for (const auto& [other, b] : s.online.balls) { if (id != other && glm::length(ball.position - b.position) < 0.1f) { return false; } }
        }
        return s.online.balls.size() == 5;
    })) || !run.require("warmup golfer's ball became their scored first-tee ball", s.online.balls.at(s.online.account_id).kind == ball_kind::scored &&
        s.online.balls.at(s.online.account_id).hole == 0 && s.online.balls.at(s.online.account_id).stroke_count == 0)) { return false; }
    // The overlapping shots below need no handling between them.
    for (std::size_t i = 1; i < 3; ++i) {
        auto& each = run.state(i); move_bag(each);
        if (!run.require("bag set down through accepted handling", run.until([&] {
            return !each.handling && each.online.bags.at(each.online.account_id).place.where == bag_where::on_ground;
        }))) { return false; }
    }
    const auto xp_before = skill_xp(s.online.progress.skills, s.rewards.shot.skill_id);
    const auto sent = s.online.server_now;
    if (!strike(run, 0, false, 0.6f) || !run.require("deliberately early first strike writes actual start with negative lateness", run.until([&] {
        return s.online.reservation && s.online.reservation->started_at > 0 && s.online.reservation->started_at < run.due;
    }))) { return false; }
    print_start(run, 0);
    // A2: a ball in flight is not yet at its rest point for another golfer's shot.
    const auto first = *last_shot(run, 0);
    const auto flight = static_cast<std::int64_t>(static_cast<double>(room_shot_result(s, first).duration) * micros);
    const auto seen = s.online.server_now;
    if (!strike(run, 1)) { return false; }
    std::printf("first flight %.2f s; second shot seen %.2f s after the first was sent\n", static_cast<double>(flight) / micros,
                static_cast<double>(run.state(1).online.server_now - sent) / micros);
    if (!run.require("precondition: second golfer struck while the first ball was still in flight", run.state(1).online.server_now + micros / 4 < sent + flight)) { return false; }
    const auto second = *last_shot(run, 1);
    bool resting = true;
    for (std::size_t i = 2; i < 5; ++i) { resting = resting && in_snapshot(second, s.online.balls.at(run.state(i).online.account_id).position); }
    if (!run.require("in-flight ball absent from another's shot snapshot; resting tee balls present", !in_snapshot(second, first.rest) && resting) ||
        !wait_for_clock(run, 2, seen + flight + micros / 2, "first ball at rest on the server clock") || !strike(run, 2) ||
        !run.require("rested ball present in a later shot's snapshot", in_snapshot(*last_shot(run, 2), s.online.balls.at(s.online.account_id).position))) { return false; }
    // A shot's XP is paid when its flight is final (its rest plus the rewind window), not at the swing.
    if (!run.require("first shot's XP paid once its flight landed", run.until([&] {
        return skill_xp(s.online.progress.skills, s.rewards.shot.skill_id) >= xp_before + s.rewards.shot.xp;
    }))) { return false; }
    for (std::size_t i = 1; i < 3; ++i) {
        if (!run.require("early first strikes write starts", run.until([&] { const auto& r = run.state(i).online.reservation; return r && r->started_at > 0 && r->started_at < run.due; }))) { return false; }
        print_start(run, i);
    }
    run.wait(4);
    run.action(0, net_command_type::give_up_hole);
    if (!run.require("give up removes ball without inventing a completed score", run.until([&] {
        for (std::size_t i = 0; i < 5; ++i) {
            const auto member = run.state(i).online.players.find(s.online.account_id);
            if (member == run.state(i).online.players.end() || member->second.round_strokes.empty() || member->second.round_strokes.front() != 0) { return false; }
        }
        return !s.online.balls.count(s.online.account_id);
    }))) { return false; }
    if (!run.walk(0, s.world.hole_starts.at(1).position, 120)) { return false; }
    // The later-hole request names the actual reached tee, with no first-tee transition.
    net_command later; later.type = net_command_type::tee_up; later.hole_index = 1; run.send(0, later);
    if (!run.require("subsequent hole keeps unplayed card blank", run.until([&] {
        const auto b = s.online.balls.find(s.online.account_id);
        const auto member = my_room_player(s);
        return b != s.online.balls.end() && b->second.hole == 1 && member && !member->round_strokes.empty() && member->round_strokes.front() == 0;
    }))) { return false; }
    const auto ball = s.online.balls.at(s.online.account_id);
    const auto scores = my_room_player(s)->round_strokes;
    run.action(0, net_command_type::leave_group);
    if (!run.require("started group leave preserves ball, scores and world", run.until([&] {
        const auto b = s.online.balls.find(s.online.account_id);
        const auto member = my_room_player(s);
        return member && member->group_id == 0 && b != s.online.balls.end() && b->second.hole == ball.hole &&
            b->second.position == ball.position && member->round_strokes == scores && s.online.reservation && !s.online.reservation->grouped;
    }))) { return false; }
    for (std::size_t i = 0; i < 6; ++i) {
        if (!run.require("no unexpected reducer refusal or prediction breach", run[i].failures.empty() && run.state(i).motion_sync.breaches == 0)) { return false; }
    }
    return true;
}
