#include "check_runtime.h"
#include "game/address_view.h"
#include "game/my_balls.h"
#include "game/mode_dispatch.h"
#include "game/movement_step.h"
#include "game/sent_setup.h"
#include "game/server_errors.h"
#include "physics/vector_math.h"
#include <algorithm>
#include <cstdio>
#include <vector>

namespace {
// A malformed client's motion claim, sent through the real SDK as one: the
// harness numbers its steps after the game's own. Returns its motion id.
double claim(live_check& run, const motion_mode mode, const glm::vec3 position, const std::vector<move_run>& runs = {},
             const glm::vec3 start = glm::vec3(0.0f), const sent_setup setup = {}) {
    auto& s = run.state(0);
    send_motion_now(s); run.tick();
    net_command c; c.type = net_command_type::motion;
    c.motion.mode = mode; c.motion.position = position; c.motion.yaw = s.player.yaw; c.motion.setup = setup;
    c.first_step = s.motion_sync.step + 1; c.start = start; c.runs = runs;
    for (const auto& r : runs) { s.motion_sync.step += r.steps; }
    run.send(0, c); run.tick();
    return s.motion_sync.last_sent ? s.motion_sync.last_sent->client_time : 0.0;
}
bool accepted(live_check& run, const double id) {
    const auto& s = run.state(0);
    return run.until([&] { const auto me = my_room_player(s); return id != 0.0 && me && me->motion && me->motion->client_time >= id; });
}
glm::vec3 row(live_check& run) { return my_room_player(run.state(0))->motion->position; }
bool unmoved(live_check& run, const char* label, const glm::vec3 before) {
    run.wait(0.5f);
    const auto after = row(run);
    std::printf("  row before (%.3f,%.3f,%.3f) after (%.3f,%.3f,%.3f)\n", before.x, before.y, before.z, after.x, after.y, after.z);
    return run.require(label, glm::length(after - before) < 0.05f);
}

// A1: positions come from the server's own state, never the claim.
bool check_claims(live_check& run) {
    auto& s = run.state(0);
    const auto idle_step = std::vector<move_run>{move_run{move_input{move_kind::walk, 0, 0, false, false, quantise_yaw(s.player.yaw)}, 1}};
    for (const float dy : {500.0f, -500.0f}) {
        const auto before = row(run);
        const auto id = claim(run, motion_mode::idle, s.player.position, idle_step, before + glm::vec3(0, dy, 0));
        if (!run.require("idle step claiming a start far above/below is played", accepted(run, id)) ||
            !unmoved(run, "idle step at the same XZ keeps the server's height", before)) { return false; }
    }
    auto before = row(run);
    claim(run, motion_mode::ride, s.player.position + glm::vec3(1, 0, 0));
    if (!run.refusal(0, reducer_update_motion, error_not_in_cart) || !unmoved(run, "unseated ride claim leaves the body", before)) { return false; }

    net_command drop; drop.type = net_command_type::place_practice_ball; drop.placed = s.player.position; run.send(0, drop);
    if (!run.require("own ball for address claims", run.until([&] { return s.online.balls.count(s.online.account_id) != 0; }))) { return false; }
    const auto ball = s.online.balls.at(s.online.account_id).position;
    sent_setup setup; setup.aim = quantise_yaw(s.player.yaw);
    const auto bag = s.online.bags.at(s.online.account_id);
    const auto club = std::find_if(s.clubs.begin(), s.clubs.end(), [&](const auto& c) { return c.id == bag.club_in_hand; });
    if (club != s.clubs.end()) { setup = clamped_sent_setup(setup, *club, s.tuning.setup); }
    shot_setup stood; stood.ball_start = ball; stood.hand = my_room_player(s)->hand;
    const auto stance = stance_at_address(s.area, with_sent_setup(stood, setup), s.tuning).position;
    const auto offset = s.player.position + glm::vec3(2.5f, 0, 0);
    const auto id = claim(run, motion_mode::address, offset, {}, glm::vec3(0.0f), setup);
    if (!run.require("address beside own ball accepted", accepted(run, id))) { return false; }
    std::printf("  address claim (%.2f,%.2f,%.2f) stance (%.2f,%.2f,%.2f) row (%.2f,%.2f,%.2f)\n", offset.x, offset.y, offset.z,
                stance.x, stance.y, stance.z, row(run).x, row(run).y, row(run).z);
    if (!run.require("address stands the server at its own stance, not the claim",
                     my_room_player(s)->motion->mode == motion_mode::address && glm::length(row(run) - stance) < 0.05f)) { return false; }

    // Out of the ball's reach (interaction radius plus slack), through accepted steps.
    const auto reach = s.tuning.player.ball_interact_radius + s.tuning.server.interact_slack;
    const auto away = ball + safe_normalize(horizontal(s.area.bus->position - ball), glm::vec3(1, 0, 0)) * (reach + 1.5f);
    if (!run.walk(0, away) || !run.require("precondition: out of the ball's reach", horizontal_distance(row(run), ball) > reach + 0.5f)) { return false; }
    before = row(run);
    claim(run, motion_mode::address, ball);
    if (!run.refusal(0, reducer_update_motion, error_too_far) || !unmoved(run, "out-of-reach address claim leaves the body", before)) { return false; }
    if (!run.walk(0, ball)) { return false; }
    run.action(0, net_command_type::pick_up_ball);
    if (!run.require("ball picked up for no-ball claim", run.until([&] { return !s.online.balls.count(s.online.account_id); }))) { return false; }
    before = row(run);
    claim(run, motion_mode::address, before + glm::vec3(1, 0, 0));
    return run.refusal(0, reducer_update_motion, error_too_far) && unmoved(run, "no-ball address claim leaves the body", before);
}
}

bool check_practice(live_check& run) {
    auto& state = run.state(0);
    const auto progress = save_data_to_json(state.online.progress);
    net_command drop; drop.type = net_command_type::place_practice_ball; drop.placed = state.player.position;
    run.send(0, drop);
    if (!run.require("practice drop adopts a hole-free authoritative ball", run.until([&] {
        const auto found = state.online.balls.find(state.online.account_id);
        return found != state.online.balls.end() && found->second.kind == ball_kind::practice && found->second.hole == -1;
    }))) { return false; }
    // The hub drop is never refused (gameplay rules): it replaces the last ball,
    // in reach of the request, else at the accepted feet.
    const auto ball_near = [&](const glm::vec3 at) {
        const auto found = state.online.balls.find(state.online.account_id);
        return found != state.online.balls.end() && found->second.kind == ball_kind::practice && horizontal_distance(found->second.position, at) < 0.05f;
    };
    drop.placed += glm::vec3(1, 0, 0); run.send(0, drop);
    if (!run.require("second drop replaces the ball at the requested point in reach", run.until([&] { return ball_near(drop.placed); }))) { return false; }
    // The ball it replaced lies on as my spare; every ball out is one array of the hub's balls_out.
    const auto spares = [&] {
        std::vector<spare_ball> mine;
        for (const auto& [id, spare] : state.online.spare_balls) { if (spare.account == state.online.account_id) { mine.push_back(spare); } }
        return mine;
    };
    if (!run.require("the replaced ball lies on as one spare", run.until([&] { return spares().size() == 1; }))) { return false; }
    const int cap = state.place.rules.balls_out;
    const auto here = my_room_player(state)->motion->position;
    for (int i = 0; i < cap + 2; ++i) {
        drop.placed = here + glm::vec3(0.2f * static_cast<float>(i % 5), 0, 0.2f * static_cast<float>(i / 5));
        run.send(0, drop); run.tick();
    }
    if (!run.require("drops fill the array: spares plus the ball in play are the cap", run.until([&] {
        return static_cast<int>(spares().size()) == cap - 1;
    }))) { return false; }
    run.wait(0.5f);
    if (!run.require("the cap holds, the oldest having gone", static_cast<int>(spares().size()) == cap - 1)) { return false; }
    // A swap is one transaction: the spare becomes the ball in play, the ball played a spare.
    const spare_ball chosen = spares().front();
    const glm::vec3 played = state.online.balls.at(state.online.account_id).position;
    net_command swap; swap.type = net_command_type::play_ball; swap.ball_id = chosen.id;
    run.send(0, swap);
    if (!run.require("play_ball swaps the spare into play and the ball in play out", run.until([&] {
        const auto ball = state.online.balls.find(state.online.account_id);
        const bool played_out = std::any_of(state.online.spare_balls.begin(), state.online.spare_balls.end(), [&](const auto& entry) {
            return entry.second.account == state.online.account_id && horizontal_distance(entry.second.position, played) < 0.05f; });
        return ball != state.online.balls.end() && horizontal_distance(ball->second.position, chosen.position) < 0.05f &&
            state.online.spare_balls.count(chosen.id) == 0 && played_out && static_cast<int>(spares().size()) == cap - 1;
    }))) { return false; }
    // Lifting one by one: each spare in reach goes, the ball in play stays.
    std::size_t lifted = 0;
    for (const spare_ball& spare : spares()) {
        if (horizontal_distance(spare.position, here) > state.tuning.player.ball_interact_radius) { continue; }
        net_command lift; lift.type = net_command_type::pick_up_ball; lift.ball_id = spare.id;
        run.send(0, lift);
        if (!run.require("pick_up_ball removes just that spare", run.until([&] { return state.online.spare_balls.count(spare.id) == 0; }))) { return false; }
        ++lifted;
    }
    if (!run.require("spares in reach were lifted and the ball in play stayed",
            lifted > 0 && state.online.balls.count(state.online.account_id) == 1)) { return false; }
    run.action(0, net_command_type::pick_up_ball);
    if (!run.require("practice pickup removes the whole ball row without progress", run.until([&] {
        return !state.online.balls.count(state.online.account_id);
    }) && save_data_to_json(state.online.progress) == progress)) { return false; }
    const auto feet = my_room_player(state)->motion->position;
    drop.placed += glm::vec3(100, 0, 100); run.send(0, drop);
    return run.require("out-of-reach drop request lands at the accepted feet", run.until([&] { return ball_near(feet); })) && check_claims(run);
}
