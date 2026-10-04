#include "game/mode_dispatch.h"

#include "game/progress_rules.h"
#include "game/progression.h"
#include "game/remote_players.h"
#include "game/round_state.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {
void queue_xp_drop(game_state& state, const std::string& skill_id, const int xp) {
    for (xp_drop& drop : state.xp_drops) {
        if (drop.skill_id == skill_id) {
            drop.xp += xp;
            drop.age = 0.0f;
            return;
        }
    }
    state.xp_drops.push_back(xp_drop{skill_id, xp, 0.0f});
}

// Shows gains as XP drops; gains below min_visible_xp are pooled per skill
// until they add up to a visible drop.
void show_awarded_xp(game_state& state, const std::vector<awarded_xp>& awarded) {
    const int min_visible = state.tuning.xp_drops.min_visible_xp;
    for (const awarded_xp& gain : awarded) {
        int& pending = state.pending_xp_drop_amounts[gain.skill_id];
        pending += gain.xp;
        if (pending >= min_visible) {
            queue_xp_drop(state, gain.skill_id, pending);
            pending = 0;
        }
    }
}

void apply_progress_update(game_state& state, const progress_update& update) {
    state.save = update.progress;
    show_awarded_xp(state, update.awarded);
}

// Adds XP to the offline save and shows it as an XP drop.
void award_skill_xp(game_state& state, const xp_reward& reward) {
    apply_progress_update(state, award_xp(state.save, reward));
}

void push_command(game_state& state, net_command command) {
    state.net_commands.push_back(std::move(command));
}
}

bool is_online(const game_state& state) {
    return state.play == play_mode::online;
}

const save_data& active_progress(const game_state& state) {
    return is_online(state) ? state.online.progress : state.save;
}

void record_shot(game_state& state, const shot_input& input) {
    if (!is_online(state)) {
        award_skill_xp(state, state.rewards.shot);
        return;
    }
    net_command command;
    command.type = net_command_type::take_shot;
    command.shot = input;
    command.stroke = state.stroke_count;
    push_command(state, std::move(command));
}

void record_emote(game_state& state, const emote_id emote) {
    if (!is_online(state)) {
        if (emote == emote_id::smoke) {
            award_skill_xp(state, state.rewards.smoke);
        }
        return;
    }
    net_command command;
    command.type = net_command_type::emote;
    command.emote = emote;
    push_command(state, std::move(command));
}

void record_movement(game_state& state, const movement_xp_rate& rate, float& pending_meters, const float meters) {
    if (is_online(state) || meters <= 0.0f) {
        return;
    }
    const movement_xp_update update = award_movement_xp(state.save, rate, pending_meters, meters);
    pending_meters = update.remainder_meters;
    apply_progress_update(state, update.update);
}

void record_collectible_claim(game_state& state, const course_world_collectible& collectible) {
    if (!is_online(state)) {
        apply_progress_update(state, claim_collectible(state.save, collectible).update);
        return;
    }
    net_command command;
    command.type = net_command_type::claim_collectible;
    command.collectible_id = collectible.id;
    push_command(state, std::move(command));
    state.online.pending_claims.push_back(collectible.id);
}

void record_hole_started(game_state& state, const std::size_t hole_index) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::enter_hole;
    command.hole_index = hole_index;
    push_command(state, std::move(command));
}

void record_retee(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::retee;
    push_command(state, std::move(command));
}

void record_hole_given_up(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::return_to_hub;
    push_command(state, std::move(command));
}

void request_group(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::create_group;
    if (const std::optional<std::uint64_t> near = nearest_player(state, state.tuning.net.group_join_distance)) {
        const std::uint64_t group = state.online.players.at(*near).group_id;
        const auto size = state.online.group_sizes.find(group);
        if (group != 0 && size != state.online.group_sizes.end() && size->second < state.tuning.server.group_capacity) {
            command.type = net_command_type::join_group;
            command.group_id = group;
        }
    }
    push_command(state, std::move(command));
}

void request_leave_group(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::leave_group;
    push_command(state, std::move(command));
}

const room_player* my_room_player(const game_state& state) {
    const auto me = state.online.players.find(state.online.account_id);
    return me != state.online.players.end() ? &me->second : nullptr;
}

float shot_wind_time(const game_state& state) {
    const room_player* me = my_room_player(state);
    if (!is_online(state) || me == nullptr || !state.hole || me->zone != static_cast<int>(state.hole->index) ||
        state.online.server_now == 0) {
        return state.hole_time;
    }
    return std::max(0.0f, server_seconds_between(me->hole_started_at, state.online.server_now));
}

bool cigarette_lit(const game_state& state) {
    if (!is_online(state)) {
        return state.cigarette_seconds_left > 0.0f;
    }
    const std::int64_t lit_at = state.online.smoke_accepted_at;
    return lit_at != 0 && server_seconds_between(lit_at, state.online.server_now) < state.rewards.cigarette.duration_seconds;
}

void record_hole_completed(game_state& state) {
    if (is_online(state)) {
        return;
    }
    state.save = apply_hole_completed(state.save);
    if (round_finished(state.round) && !state.course.practice) {
        state.save = apply_course_completed(state.save, state.course.id);
    }
    state.save_requested = true;
}

namespace {
// Whether `progress` shows `collectible_id` claimed since `before`.
bool newly_claimed(const save_data& before, const save_data& progress, const std::string& collectible_id) {
    const auto has = [&collectible_id](const std::vector<std::string>& ids) {
        return std::find(ids.begin(), ids.end(), collectible_id) != ids.end();
    };
    if (has(progress.collected_ids) && !has(before.collected_ids)) {
        return true;
    }
    const auto now = progress.repeatable_collectibles.find(collectible_id);
    if (now == progress.repeatable_collectibles.end()) {
        return false;
    }
    const auto was = before.repeatable_collectibles.find(collectible_id);
    const int claimed_before = was == before.repeatable_collectibles.end() ? 0 : was->second.claim_count;
    return now->second.claim_count > claimed_before;
}
}

void receive_online_progress(game_state& state, const save_data& progress) {
    std::vector<awarded_xp> gained;
    for (const auto& [skill_id, skill] : progress.skills) {
        const int gain = skill.xp - skill_xp(state.online.progress.skills, skill_id);
        if (gain > 0 && state.online.progress_received) {
            gained.push_back(awarded_xp{skill_id, gain});
        }
    }
    // A claim stays pending until its own answer: here, the collectible
    // showing up claimed (other progress, movement XP say, comes meanwhile).
    std::vector<std::string>& pending = state.online.pending_claims;
    pending.erase(std::remove_if(pending.begin(), pending.end(),
                                 [&](const std::string& id) { return newly_claimed(state.online.progress, progress, id); }),
                  pending.end());
    state.online.progress = progress;
    state.online.progress_received = true;
    show_awarded_xp(state, gained);
}
