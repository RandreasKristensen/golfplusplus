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
    apply_progress_update(state, award_xp(state.save, reward, current_xp_effects(state)));
}

void push_command(game_state& state, net_command command) {
    state.net_commands.push_back(std::move(command));
}

// Online: pushes a command of `type` with no fields. Offline: nothing.
void push_online_command(game_state& state, const net_command_type type) {
    if (is_online(state)) {
        net_command command;
        command.type = type;
        push_command(state, std::move(command));
    }
}
}

bool is_online(const game_state& state) {
    return state.play == play_mode::online;
}

const save_data& active_progress(const game_state& state) {
    return is_online(state) ? state.online.progress : state.save;
}

golfer_profile my_golfer(const game_state& state) {
    golfer_profile golfer;
    golfer.hand = state.hand;
    if (!is_online(state) && state.save.natural) {
        golfer.natural = *state.save.natural;
    }
    if (const room_player* me = my_room_player(state)) {
        golfer.natural = natural_shape_from_index(me->natural_shape).value_or(natural_shape::neutral);
    }
    golfer.spread_scale = effective_value(1.0f, effect_target_shot_spread, current_effects(state), shot_unix_ms(state));
    return golfer;
}

bool natural_shape_picked(const game_state& state) {
    if (!is_online(state)) {
        return state.save.natural.has_value();
    }
    const room_player* me = my_room_player(state);
    return me == nullptr || natural_shape_from_index(me->natural_shape).has_value();
}

void record_natural_shape(game_state& state, const natural_shape shape) {
    if (!is_online(state)) {
        state.save.natural = shape;
        return;
    }
    net_command command;
    command.type = net_command_type::set_golfer;
    command.hand = state.hand;
    command.natural = static_cast<std::uint8_t>(natural_shape_index(shape));
    state.golfer_sent = std::make_pair(command.hand, command.natural);
    push_command(state, std::move(command));
}

void sync_golfer(game_state& state) {
    const room_player* me = is_online(state) ? my_room_player(state) : nullptr;
    if (me == nullptr || me->hand == state.hand) {
        return;
    }
    const std::pair<handedness, std::uint8_t> wanted{state.hand, me->natural_shape};
    if (state.golfer_sent == wanted) {
        return;
    }
    net_command command;
    command.type = net_command_type::set_golfer;
    command.hand = wanted.first;
    command.natural = wanted.second;
    state.golfer_sent = wanted;
    push_command(state, std::move(command));
}

std::uint64_t next_shot_seed(const game_state& state) {
    return round_shot_seed(state.round_seed, state.shots_hit);
}

void record_shot(game_state& state, const shot_setup& setup) {
    if (!is_online(state)) {
        award_skill_xp(state, state.rewards.shot);
        return;
    }
    net_command command;
    command.type = net_command_type::take_shot;
    command.shot = setup;
    command.stroke = state.ball ? state.ball->strokes + 1 : 0;
    for (std::size_t hole = 0; hole < state.flags_in.size(); ++hole) {
        if (!state.flags_in[hole]) {
            command.flags_out.push_back(static_cast<int>(hole));
        }
    }
    push_command(state, std::move(command));
}

void record_flag(game_state& state, const std::size_t hole, const bool out) {
    if (!is_online(state)) {
        if (hole < state.flags_in.size()) {
            state.flags_in[hole] = !out;
        }
        return;
    }
    net_command command;
    command.type = net_command_type::set_flag;
    command.hole_index = hole;
    command.flag_out = out;
    push_command(state, std::move(command));
}

void record_bag_moved(game_state& state) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::place_bag;
    command.bag = state.bag;
    push_command(state, std::move(command));
}

void record_club_taken(game_state& state, const std::string& club_id) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::take_club;
    command.club_id = club_id;
    push_command(state, std::move(command));
}

void record_item_use(game_state& state, const item_definition& item) {
    if (is_online(state)) {
        net_command command;
        command.type = net_command_type::use_item;
        command.item_id = item.id;
        push_command(state, std::move(command));
        return;
    }
    if (item.use && item.use->xp_per_use) {
        award_skill_xp(state, *item.use->xp_per_use);
    }
    const std::int64_t now = shot_unix_ms(state);
    state.effects = effects_after_use(running_effects(state.effects, now), item, now);
}

loadout equipped_loadout(const game_state& state) {
    if (!is_online(state)) {
        return fitted_loadout(state.save.equipped, state.default_loadout, state.items);
    }
    const auto mine = state.online.loadouts.find(state.online.account_id);
    return mine != state.online.loadouts.end() ? fitted_loadout(mine->second, state.default_loadout, state.items)
                                               : state.default_loadout;
}

std::vector<active_effect> current_effects(const game_state& state) {
    std::vector<active_effect> effects = equipped_effects(equipped_loadout(state), state.items);
    const std::vector<active_effect>& timed = is_online(state) ? state.online.effects : state.effects;
    effects.insert(effects.end(), timed.begin(), timed.end());
    return effects;
}

xp_effects current_xp_effects(const game_state& state) {
    return xp_effects{current_effects(state), shot_unix_ms(state)};
}

void record_movement(game_state& state, const movement_xp_rate& rate, float& pending_meters, const float meters) {
    if (is_online(state) || meters <= 0.0f) {
        return;
    }
    const movement_xp_update update = award_movement_xp(state.save, rate, pending_meters, meters, current_xp_effects(state));
    pending_meters = update.remainder_meters;
    apply_progress_update(state, update.update);
}

void record_collectible_claim(game_state& state, const course_world_collectible& collectible) {
    if (!is_online(state)) {
        apply_progress_update(state, claim_collectible(state.save, collectible, current_xp_effects(state)).update);
        return;
    }
    net_command command;
    command.type = net_command_type::claim_collectible;
    command.collectible_id = collectible.id;
    push_command(state, std::move(command));
    state.online.pending_claims.push_back(collectible.id);
}

void record_tee_up(game_state& state, const std::size_t hole_index, const bool give_up) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::tee_up;
    command.hole_index = hole_index;
    command.give_up = give_up;
    push_command(state, std::move(command));
}

void record_ball_picked_up(game_state& state) {
    push_online_command(state, net_command_type::pick_up_ball);
}

void record_hole_given_up(game_state& state) {
    push_online_command(state, net_command_type::give_up_hole);
}

void record_cart_boarded(game_state& state, const std::uint64_t owner) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::board_cart;
    command.cart_owner = owner;
    push_command(state, std::move(command));
}

void record_cart_left(game_state& state) {
    push_online_command(state, net_command_type::leave_cart);
}

void record_cart_overturned(game_state& state, const cart_condition condition) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::overturn_cart;
    command.overturned_to = condition;
    push_command(state, std::move(command));
}

void record_cart_righted(game_state& state, const std::uint64_t owner) {
    if (!is_online(state)) {
        return;
    }
    net_command command;
    command.type = net_command_type::right_cart;
    command.cart_owner = owner != 0 ? owner : state.online.account_id;
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
    push_online_command(state, net_command_type::leave_group);
}

std::int64_t shared_unix_ms(const game_state& state, const std::int64_t local_unix_ms) {
    constexpr std::int64_t microseconds_per_millisecond = 1000;
    return is_online(state) && state.online.server_now != 0 ? state.online.server_now / microseconds_per_millisecond
                                                            : local_unix_ms;
}

const room_player* my_room_player(const game_state& state) {
    const auto me = state.online.players.find(state.online.account_id);
    return me != state.online.players.end() ? &me->second : nullptr;
}

std::int64_t shot_unix_ms(const game_state& state) {
    return shared_unix_ms(state, state.local_unix_ms);
}

weather_state course_weather_at(const game_state& state, const std::int64_t unix_ms) {
    if (!is_online(state) && state.weather_override) {
        return *state.weather_override;
    }
    const game_tuning& tuning = state.tuning;
    return weather_at(tuning.weather, tuning.wind, tuning.sky_clock, state.course, unix_ms);
}

void record_hole_completed(game_state& state) {
    if (is_online(state)) {
        return;
    }
    state.save = apply_hole_completed(state.save);
    if (round_finished(state.round)) {
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
