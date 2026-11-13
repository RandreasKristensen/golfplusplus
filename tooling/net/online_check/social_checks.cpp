#include "check_runtime.h"
#include "game/mode_dispatch.h"
#include "game/progression.h"
#include "game/social_dap.h"
#include "game/server_errors.h"
#include <algorithm>

bool check_social(live_check& run) {
    auto& a = run.state(0); auto& b = run.state(1);
    const auto first = a.online.account_id, second = b.online.account_id;
    if (!run.walk(0, a.world.hub->book.position + glm::vec3(-1, 0, -1)) ||
        !run.walk(1, a.player.position + glm::vec3(1, 0, 0))) { return false; }
    const auto xp_a = skill_xp(a.online.progress.skills, "social"), xp_b = skill_xp(b.online.progress.skills, "social");
    const auto xp = a.rewards.dap.xp;
    request_dap(a, second);
    if (!run.require("participants and observer see same accepted dap row/time", run.until([&] {
        if (a.online.daps.empty()) { return false; }
        const auto& [id, row] = *a.online.daps.begin();
        for (std::size_t i = 1; i < 3; ++i) {
            const auto other = run.state(i).online.daps.find(id);
            if (other == run.state(i).online.daps.end() || other->second.started_at != row.started_at ||
                other->second.first != first || other->second.second != second) { return false; }
        }
        return skill_xp(a.online.progress.skills, "social") == xp_a + xp && skill_xp(b.online.progress.skills, "social") == xp_b + xp;
    }))) { return false; }
    net_command dap; dap.type = net_command_type::dap; dap.target_account = second; run.send(0, dap);
    if (!run.refusal(0, "dap", error_not_available)) { return false; }
    run.wait(a.tuning.dap.seconds + 0.2f);
    if (!run.require("animation/refused repeat pays no extra Social XP", skill_xp(a.online.progress.skills, "social") == xp_a + xp && skill_xp(b.online.progress.skills, "social") == xp_b + xp)) { return false; }
    const auto original = a.online.daps.begin()->first;
    request_dap(a, second);
    if (!run.require("next dap accepted and each recipient paid exactly once", run.until([&] {
        return !a.online.daps.empty() && a.online.daps.begin()->first != original &&
            skill_xp(a.online.progress.skills, "social") == xp_a + 2 * xp && skill_xp(b.online.progress.skills, "social") == xp_b + 2 * xp;
    }))) { return false; }
    const auto sixth = run.state(5).online.account_id;
    const auto later = run.state(5).online.reservation->scheduled_at;
    if (!run.walk(5, run.state(2).player.position + glm::vec3(-1, 0, 0))) { return false; }
    request_dap(run.state(2), sixth);
    std::uint64_t departing_pair = 0;
    if (!run.require("disconnect scenario has a live accepted pair", run.until([&] {
        for (const auto& [id, row] : run.state(2).online.daps) {
            if (row.first == run.state(2).online.account_id && row.second == sixth) { departing_pair = id; return true; }
        }
        return false;
    }))) { return false; }
    run[5].client.sign_out(run.state(5));
    if (!run.require("guest disconnect removes presence/reservation count", run.until([&] {
        if (a.online.players.count(sixth) || run.state(2).online.daps.count(departing_pair)) { return false; }
        // The released time's row goes once nobody holds it.
        for (const auto& [id, slot] : a.online.tee_slots) { if (slot.scheduled_at == later) { return slot.booked_count == 0; } }
        return true;
    }))) { return false; }
    run[5].client.begin_login();
    if (!run.require("guest reconnect adopts new account", run.until([&] { return run.state(5).online.account_id && run.state(5).online.account_id != sixth; }))) { return false; }
    run[5].client.claim_name("CHECK" + std::to_string(run.state(5).online.account_id));
    if (!run.until([&] { return !run.state(5).online.display_name.empty(); })) { return false; }
    run[5].client.join_course(online_hub_course, false);
    return run.require("reconnect adopts fresh hub snapshot without reservation or old pair", run.until([&] {
        const auto& s = run.state(5); return s.online.room_ready && run[5].adopted_room && s.online.room && s.online.room->course_id == online_hub_course && !s.online.reservation && s.online.players.size() == 6;
    }));
}
