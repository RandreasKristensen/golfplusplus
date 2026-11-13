#include "check_runtime.h"
#include "game/tee_times.h"
#include "game/hub_flow.h"
#include "game/mode_dispatch.h"
#include "game/golf_bag.h"
#include "game/hub_scene.h"
#include "game/progression.h"
#include "game/remote_players.h"
#include "game/server_errors.h"
#include "game/scorecard.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <cstdio>
namespace {
bool strike(live_check& run, const bool reject_iron = false) {
    auto& s = run.state(0);
    const auto& ball = s.online.balls.at(s.online.account_id);
    const auto& bag = s.online.bags.at(s.online.account_id);
    const auto club = std::find_if(s.clubs.begin(), s.clubs.end(), [&](const auto& c) { return c.id == bag.club_in_hand; });
    if (!run.require("server supplied club exists", club != s.clubs.end())) { return false; }
    net_command c; c.type = net_command_type::take_shot; c.stroke = ball.stroke_count + 1;
    c.shot = stock_setup(*club, lie_at(s.area, ball.position), ball.position,
        yaw_towards(ball.position, s.holes.at(static_cast<std::size_t>(ball.hole)).pin_position), handedness::right, 0, s.tuning.setup);
    c.shot.backswing = 0.2f; c.shot.shot_at = shot_unix_ms(s);
    c.flags_out = s.online.flags_out;
    if (reject_iron) {
        c.shot.club_id = s.clubs.front().id; run.send(0, c);
        return run.refusal(0, reducer_take_shot, error_not_available);
    }
    const auto before = run[0].shots.size(); run.send(0, c);
    if (!run.require("actual server shot accepted", run.until([&] { return run[0].shots.size() > before; }))) { return false; }
    const auto& shot = run[0].shots.back();
    const auto simulated = room_shot_result(s, shot);
    return run.require("live WASM shot rest agrees with native replay", shot.account_id == s.online.account_id && glm::length(shot.rest - simulated.rest_position) < 0.001f);
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
    if (!strike(run, true) || !strike(run)) { return false; }
    run.wait(4);
    if (!run.require("warmup awards no XP/score/completion/start", save_data_to_json(s.online.progress) == before && s.online.reservation->started_at == 0 &&
        std::all_of(my_room_player(s)->round_strokes.begin(), my_room_player(s)->round_strokes.end(), [](int score) { return score == 0; }))) { return false; }
    return run.walk(0, s.world.hole_starts.front().position, 90);
}
}
bool check_round(live_check& run) {
    // The bus opens bus_early_minutes before the booked time: wait for the real server clock.
    const auto opens = run.due - run.state(0).tuning.server.bus_early_minutes * minute_micros;
    if (!run.require("bus window opens on the server clock", run.until([&] { return run.state(0).online.server_now >= opens; }, 20 * 60))) { return false; }
    for (std::size_t i = 0; i < 5; ++i) {
        if (!run.walk(i, run.state(i).area.bus->position) || !run.travel(i, run.course)) { return false; }
        if (i == 0 && !run.require("travel removes paired row for remaining hub observer", run.until([&] { return run.state(2).online.daps.empty(); }))) { return false; }
    }
    auto& s = run.state(0);
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
    run.action(0, net_command_type::start_booked_round);
    if (!run.refusal(0, "start_booked_round", error_tee_too_early)) { return false; }
    if (!warmup(run)) { return false; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(15);
    while (s.online.server_now < run.due) {
        if (!run.require("UTC wait remains connected and bounded by fifteen minutes",
            s.online.status == net_status::connected && s.online.server_now > 0 &&
            std::chrono::steady_clock::now() < deadline)) { return false; }
        std::printf("waiting for natural UTC tee time: %.0f seconds\n", static_cast<double>(run.due - s.online.server_now) / 1000000.0);
        run.wait(std::min(30.0f, static_cast<float>(run.due - s.online.server_now) / 1000000.0f + 0.05f));
    }
    for (std::size_t i = 0; i < 5; ++i) { request_ball_preparation(run.state(i), net_command_type::start_booked_round); }
    if (!run.require("five first tee balls prepared in separate lanes without starting audit", run.until([&] {
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
    }))) { return false; }
    const auto xp_before = skill_xp(s.online.progress.skills, s.rewards.shot.skill_id);
    if (!strike(run) || !run.require("first accepted strike writes actual start and lateness", run.until([&] {
        return s.online.reservation && s.online.reservation->started_at >= run.due && skill_xp(s.online.progress.skills, s.rewards.shot.skill_id) >= xp_before + s.rewards.shot.xp;
    }))) { return false; }
    std::printf("actual start microseconds: %lld; lateness microseconds: %lld\n", static_cast<long long>(s.online.reservation->started_at), static_cast<long long>(s.online.reservation->started_at - run.due));
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
