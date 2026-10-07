#include "game/game_state.h"

#include "game/course_session.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/online_play.h"
#include "game/progress_rules.h"
#include "game/remote_players.h"
#include "game/shot_simulation.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// Longer frames are played as this long, so a hitch does not jump the player
// or a playing shot far ahead.
constexpr float max_update_seconds = 0.05f;

void push_audio_event(game_state& state, const audio_event_type type) {
    audio_event event;
    event.type = type;
    state.audio_events.push_back(event);
}

const club_definition* selected_club(const game_state& state) {
    return state.selected_club < state.clubs.size() ? &state.clubs[state.selected_club] : nullptr;
}

// The selected club's stats with the cigarette modifiers applied.
club_stats effective_club_stats(const game_state& state) {
    const club_definition* club = selected_club(state);
    return shot_club_stats(club != nullptr ? club->stats : club_stats{}, cigarette_lit(state), state.rewards);
}

void append_flight_path_point(game_state& state, const glm::vec3& position) {
    const flight_path_tuning& tuning = state.tuning.flight_path;
    std::vector<glm::vec3>& points = state.flight_path_points;
    if (!points.empty() && glm::length(position - points.back()) < tuning.min_point_spacing) {
        return;
    }
    points.push_back(position);
    const std::size_t max_points = static_cast<std::size_t>(std::max(2, tuning.max_points));
    if (points.size() > max_points) {
        points.erase(points.begin(), points.begin() + static_cast<std::ptrdiff_t>(points.size() - max_points));
    }
}

// +1 turning left (towards +X), -1 right, 0 for neither or both.
float turn_direction(const game_input& input) {
    return static_cast<float>(input.turn_left_held) - static_cast<float>(input.turn_right_held);
}

// +1 forward, -1 back, 0 for neither or both.
float walk_direction(const game_input& input) {
    return static_cast<float>(input.forward_held) - static_cast<float>(input.back_held);
}

// An emote does not restart while it plays, online or off (the server
// refuses it too), so smoking earns its XP at most once per animation.
void update_emotes(game_state& state, const game_input& input, const float dt) {
    // Not while a shot plays either: a holing shot ends the hole's emotes,
    // and online the server ends them when it is hit.
    const bool can_emote = !shot_playing(state);
    if (input.smoke && can_emote && !state.smoke_emote.active) {
        trigger_emote(state.smoke_emote);
        state.cigarette_seconds_left = state.rewards.cigarette.duration_seconds;
        record_emote(state, emote_id::smoke);
        push_audio_event(state, audio_event_type::emote_smoke);
    }
    if (input.drink && can_emote && !state.beer_emote.active) {
        trigger_emote(state.beer_emote);
        record_emote(state, emote_id::drink);
        push_audio_event(state, audio_event_type::emote_beer);
    }
    tick_emote(state.smoke_emote, state.tuning.player.emote_seconds, dt);
    tick_emote(state.beer_emote, state.tuning.player.emote_seconds, dt);
    state.cigarette_seconds_left = std::max(0.0f, state.cigarette_seconds_left - dt);
}

// Where the rangefinder points: this hole's pin, or in the hub the pin of the
// next hole to play.
std::optional<glm::vec3> rangefinder_target(const game_state& state) {
    if (state.hole) {
        return pin_anchor_position(state);
    }
    const std::vector<glm::vec3>& pins = state.static_anchors.hub_pin_markers;
    if (state.round.current_hole_index < pins.size()) {
        return pins[state.round.current_hole_index];
    }
    return std::nullopt;
}

void update_overlays(game_state& state, const game_input& input) {
    const bool walking = state.mode == game_mode::walking;
    const std::optional<glm::vec3> target = rangefinder_target(state);
    state.rangefinder_active = walking && input.rangefinder_held && !input.cart_held && target.has_value();
    state.rangefinder_distance_meters = target
        ? horizontal_distance(state.player.position, *target) * state.tuning.scale.meters_per_world_unit
        : 0.0f;
    state.course_map_active = walking && input.course_map_held;
    state.scorecard_active = walking && input.scorecard_held;
    state.skills_panel_active = input.skills_panel_held;
}

void exit_cart(game_state& state) {
    state.cart = cart_state{false, 0.0f, state.player.yaw, 0.0f};
}

void update_cart(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    if (!input.cart_held) {
        exit_cart(state);
        return;
    }

    const cart_tuning& tuning = state.tuning.cart;
    if (!state.cart.active) {
        state.cart = cart_state{true, 0.0f, state.player.yaw, 0.0f};
        push_audio_event(state, audio_event_type::cart_start);
    }

    if (input.action) {
        state.cart.drift_timer = tuning.drift_duration;
        state.cart.velocity = std::max(state.cart.velocity, tuning.speed * tuning.drift_speed_boost);
        push_audio_event(state, audio_event_type::cart_drift);
    }
    state.cart.drift_timer = std::max(0.0f, state.cart.drift_timer - dt);

    const bool drifting = state.cart.drift_timer > 0.0f;
    const bool on_road = cart_on_road(state);
    const bool hub = in_hub(state);
    const float control = !hub ? 1.0f : (on_road ? tuning.road_control_scale : tuning.off_road_control_scale);
    const float road_speed = !hub ? 1.0f : (on_road ? tuning.road_speed_scale : tuning.off_road_speed_scale);

    const float turn_rate = (drifting ? tuning.drift_turn_rate : tuning.turn_rate) * control;
    state.player.turn_rate = turn_rate * turn_direction(input);
    state.cart.yaw = wrap_angle(state.cart.yaw + state.player.turn_rate * dt);

    const float target_speed = tuning.speed * (drifting ? tuning.drift_speed_boost : 1.0f) * road_speed;
    const float damping = (drifting ? tuning.drift_damping : tuning.normal_damping) * control;
    state.cart.velocity += (target_speed - state.cart.velocity) * (1.0f - std::exp(-std::max(0.0f, damping) * dt));

    const glm::vec3 before = state.player.position;
    state.player.position += yaw_direction(state.cart.yaw) * state.cart.velocity * dt;
    state.player.position.y = terrain_height(state.area, state.player.position, profile);
    state.player.yaw = state.cart.yaw;
    state.player.speed = state.cart.velocity;

    if (on_road && std::abs(state.cart.velocity) > still_speed) {
        const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
        record_movement(state, state.rewards.cart_on_road, state.cart_meters_pending, meters);
        if (drifting) {
            record_movement(state, state.rewards.drift_on_road, state.drift_meters_pending, meters);
        }
    }
}

// A hole start is its tee: the press that starts the hole also takes up the
// ball teed there, like any press beside the ball. A ball left in its cup is
// picked up first: no hole starts before then.
void interact(game_state& state) {
    if (in_hub(state)) {
        const bool cup_ball = cup_ball_in_reach(state);
        const std::optional<std::size_t> collectible = nearby_collectible(state);
        const std::optional<std::size_t> start = nearby_hole_start(state);
        if (cup_ball || collectible || start) {
            send_motion_now(state);
        }
        if (cup_ball) {
            pick_up_cup_ball(state);
            return;
        }
        if (collectible) {
            record_collectible_claim(state, state.hub->world.collectibles[*collectible]);
            return;
        }
        if (!start || !start_hub_hole(state, *start)) {
            return;
        }
    }
    if (can_interact_with_ball(state)) {
        state.mode = game_mode::aiming;
        state.aim_angle = state.player.yaw;
        state.swing = swing_state{};
    }
}

void update_walking(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    if (input.cart_held || state.cart.active) {
        update_cart(state, input, dt, profile);
        return;
    }

    const player_tuning& tuning = state.tuning.player;
    state.player.turn_rate = tuning.turn_rate * turn_direction(input);
    state.player.yaw = wrap_angle(state.player.yaw + state.player.turn_rate * dt);

    const glm::vec3 before = state.player.position;
    state.player.speed = tuning.walk_speed * walk_direction(input);
    state.player.position += yaw_direction(state.player.yaw) * (state.player.speed * dt);
    state.player.position.y = terrain_height(state.area, state.player.position, profile);

    const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
    record_movement(state, state.rewards.walking, state.walk_meters_pending, meters);

    if (input.action) {
        interact(state);
    }
}

void change_club(game_state& state, const game_input& input) {
    if (state.clubs.empty() || (!input.longer_club && !input.shorter_club)) {
        return;
    }
    const std::size_t count = state.clubs.size();
    state.selected_club = input.shorter_club ? (state.selected_club + count - 1) % count : (state.selected_club + 1) % count;
    push_audio_event(state, audio_event_type::club_change);
}

void update_aiming(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    state.player.turn_rate = state.tuning.player.aim_turn_rate * turn_direction(input);
    state.aim_angle = wrap_angle(state.aim_angle + state.player.turn_rate * dt);
    state.player.yaw = state.aim_angle;
    change_club(state, input);

    if (input.action) {
        state.player.position =
            address_stance_position(state.area, state.ball.position, state.aim_angle, state.tuning.player, profile);
        state.player.turn_rate = 0.0f;
        state.mode = game_mode::addressing;
        state.swing = swing_state{};
    }
}

void launch_shot(game_state& state) {
    const club_definition* club = selected_club(state);
    if (club == nullptr) {
        return;
    }

    shot_input input;
    input.ball_start = state.ball.position;
    input.aim_angle = wrap_angle(state.aim_angle);
    input.club_id = club->id;
    input.power = state.swing.power;
    input.cigarette_active = cigarette_lit(state);
    input.wind_time = shot_wind_time(state);
    // Online the server checks the shot is hit from beside the ball: it
    // hears where the player stands first.
    send_motion_now(state);
    play_shot(state, simulate_shot(input, current_shot_course(state), state.tuning, state.clubs, state.rewards));
    state.swing = swing_state{};
    ++state.stroke_count;
    record_shot(state, input);

    audio_event hit;
    hit.type = audio_event_type::club_hit;
    hit.club_hit_sound = club->hit_sound;
    state.audio_events.push_back(hit);
}

void update_addressing(game_state& state, const game_input& input, const float dt) {
    change_club(state, input);

    if (state.swing.phase == swing_phase::timing) {
        state.swing.elapsed += dt;
        state.swing.power =
            swing_meter_power(state.swing.elapsed, effective_club_stats(state).timing_speed, state.tuning.swing.meter_cycle_seconds);
    }
    if (!input.action) {
        return;
    }
    if (state.swing.phase == swing_phase::idle) {
        state.swing = swing_state{swing_phase::timing, 0.0f, 0.0f};
        push_audio_event(state, audio_event_type::swing_start);
        // Online others raise their club with mine from when the server hears it.
        send_motion_now(state);
        return;
    }
    launch_shot(state);
}

void play_shot_events(game_state& state, shot_playback& shot) {
    const std::vector<shot_event>& events = shot.result.events;
    for (; shot.next_event < events.size() && events[shot.next_event].time <= shot.elapsed; ++shot.next_event) {
        const shot_event& event = events[shot.next_event];
        audio_event sound;
        sound.type = event.kind == shot_event_kind::land ? audio_event_type::ball_land : audio_event_type::ball_tree_hit;
        sound.material = event.material;
        state.audio_events.push_back(sound);
    }
}

// Moves the ball along the playing shot; a holed shot completes the hole
// once it has finished playing.
void update_shot_playback(game_state& state, const float dt) {
    if (!state.shot) {
        return;
    }
    shot_playback& shot = *state.shot;
    shot.elapsed += dt;
    const float duration = shot.result.duration;
    const float progress = duration > 0.0f ? std::min(1.0f, shot.elapsed / duration) : 1.0f;
    state.ball.position = shot_position_at(shot.result, shot.elapsed) + shot.correction * progress;
    play_shot_events(state, shot);
    append_flight_path_point(state, state.ball.position);
    if (shot.elapsed < shot.result.duration) {
        return;
    }

    const bool holed = shot.result.holed;
    if (shot.result.penalty_strokes > 0) {
        // Lost in water: the ball is back where it was hit from, a stroke later.
        state.stroke_count += shot.result.penalty_strokes;
        state.notice = game_notice{text_hud_water_penalty, 0.0f};
    }
    state.shot.reset();
    state.mode = game_mode::walking;
    state.flight_path_points.clear();
    if (holed) {
        push_audio_event(state, audio_event_type::ball_cup);
    }
    // Online the hole is over when the server says so (game/online_play.h).
    if (holed && !is_online(state)) {
        complete_current_hole(state);
    }
}
}

game_state make_game_state(const game_content& content, const save_data& save) {
    game_state state;
    state.asset_root = content.asset_root;
    state.tuning = content.tuning;
    state.clubs = content.clubs;
    state.rewards = content.rewards;
    state.save = save;
    return state;
}

void update_game(game_state& state, const game_input& input, const float raw_dt, frame_profile* profile) {
    const float dt = std::clamp(raw_dt, 0.0f, max_update_seconds);
    if (round_finished(state.round)) {
        // Online, my results wait for the group; the room plays on meanwhile.
        if (waiting_for_group(state)) {
            watch_room(state, dt);
            update_remote_players(state, dt);
        }
        return;
    }
    refresh_static_anchor_cache(state, profile);
    update_xp_drops(state, dt);
    state.hole_time += dt;
    state.player.speed = 0.0f;
    state.player.turn_rate = 0.0f;
    update_emotes(state, input, dt);
    if (input.leave_group) {
        request_leave_group(state);
    } else if (input.group) {
        request_group(state);
    }

    // A retee waits for a playing shot to finish: it may already have holed.
    if (input.retee && state.hole && !shot_playing(state)) {
        retee_ball(state);
        record_retee(state);
    } else if (input.cancel && (state.mode == game_mode::aiming || state.mode == game_mode::addressing)) {
        state.mode = game_mode::walking;
        state.swing = swing_state{};
    } else {
        switch (state.mode) {
        case game_mode::walking:
            update_walking(state, input, dt, profile);
            break;
        case game_mode::aiming:
            update_aiming(state, input, dt, profile);
            break;
        case game_mode::addressing:
            update_addressing(state, input, dt);
            break;
        case game_mode::following_shot:
            break;
        }
        if (state.mode != game_mode::walking && state.cart.active) {
            exit_cart(state);
        }
        update_shot_playback(state, dt);
    }

    update_overlays(state, input);
    update_online_play(state, dt);
    update_remote_players(state, dt);
    sync_motion(state, dt);
}

void update_xp_drops(game_state& state, const float dt) {
    const float lifetime = state.tuning.xp_drops.lifetime_seconds;
    for (xp_drop& drop : state.xp_drops) {
        drop.age += std::max(0.0f, dt);
    }
    state.xp_drops.erase(std::remove_if(state.xp_drops.begin(), state.xp_drops.end(),
                                        [lifetime](const xp_drop& drop) { return drop.age >= lifetime; }),
                         state.xp_drops.end());
}

bool in_hub(const game_state& state) {
    return state.hub.has_value() && !state.hole.has_value();
}

bool shot_playing(const game_state& state) {
    return state.shot.has_value();
}

shot_course current_shot_course(const game_state& state) {
    const std::uint32_t wind_seed = state.hole ? state.hole->wind_seed : 0;
    return shot_course{state.area, state.static_anchors.trees, shot_hole{pin_anchor_position(state), wind_seed}};
}

void play_shot(game_state& state, shot_result result) {
    if (!result.trajectory.empty()) {
        state.ball.position = result.trajectory.front();
    }
    state.shot_start_position = state.ball.position;
    state.mode = game_mode::following_shot;
    state.flight_path_points.clear();
    append_flight_path_point(state, state.ball.position);
    state.shot = shot_playback{std::move(result), 0.0f, 0};
}

bool can_interact_with_ball(const game_state& state) {
    return state.hole &&
        horizontal_distance(state.player.position, state.ball.position) <= state.tuning.player.ball_interact_radius &&
        !shot_playing(state);
}

std::optional<std::size_t> nearby_hole_start(const game_state& state) {
    if (!in_hub(state)) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    float best_distance = std::numeric_limits<float>::max();
    const std::vector<course_world_hole_start>& starts = state.hub->world.hole_starts;
    for (std::size_t i = 0; i < starts.size(); ++i) {
        const float distance = horizontal_distance(state.player.position, starts[i].position);
        if (!hole_played(state.round, i) && distance <= starts[i].interaction_radius && distance < best_distance) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

std::optional<std::size_t> nearby_collectible(const game_state& state) {
    if (!in_hub(state)) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    float best_distance = std::numeric_limits<float>::max();
    const std::vector<course_world_collectible>& collectibles = state.hub->world.collectibles;
    for (std::size_t i = 0; i < collectibles.size(); ++i) {
        const float distance = horizontal_distance(state.player.position, collectibles[i].position);
        if (distance <= collectibles[i].interaction_radius && distance < best_distance &&
            collectible_available(active_progress(state), collectibles[i]) &&
            std::find(state.online.pending_claims.begin(), state.online.pending_claims.end(), collectibles[i].id) ==
                state.online.pending_claims.end()) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

bool cart_on_road(const game_state& state) {
    return in_hub(state) && on_cart_road(state.hub->world.cart_roads, state.player.position, state.tuning.cart);
}

int rounded_rangefinder_meters(const float distance_meters) {
    return static_cast<int>(std::floor(std::max(0.0f, distance_meters) + 0.5f));
}

static_anchor_cache build_static_anchor_cache(const game_state& state, frame_profile* profile) {
    static_anchor_cache cache;
    cache.valid = true;
    cache.revision = state.terrain_render_revision;
    const play_area& area = state.area;
    if (state.hole) {
        cache.tee_anchor = anchor_on_terrain(area, state.hole->tee_position, profile);
        cache.pin_anchor = anchor_on_terrain(area, state.hole->pin_position, profile);
    }
    cache.trees = standing_trees(area, profile);
    if (state.hub) {
        for (const hub_hole_marker& marker : state.hub->markers) {
            cache.hub_pin_markers.push_back(anchor_on_terrain(area, marker.pin_position, profile));
        }
    }
    if (in_hub(state)) {
        for (const course_world_collectible& collectible : state.hub->world.collectibles) {
            cache.collectibles.push_back(anchor_on_terrain(area, collectible.position, profile));
        }
    }
    return cache;
}

bool static_anchor_cache_is_current(const game_state& state) {
    return state.static_anchors.valid && state.static_anchors.revision == state.terrain_render_revision;
}

void refresh_static_anchor_cache(game_state& state, frame_profile* profile) {
    if (!static_anchor_cache_is_current(state)) {
        state.static_anchors = build_static_anchor_cache(state, profile);
    }
}

void mark_terrain_render_dirty(game_state& state) {
    ++state.terrain_render_revision;
    refresh_static_anchor_cache(state);
}

void continue_terrain_render_revision(game_state& state, const std::uint64_t previous_revision) {
    if (state.terrain_render_revision > previous_revision) {
        return;
    }
    // The anchors were built for this state, so only the key moves.
    const bool anchors_current = static_anchor_cache_is_current(state);
    state.terrain_render_revision = previous_revision + 1;
    if (anchors_current) {
        state.static_anchors.revision = state.terrain_render_revision;
    } else {
        refresh_static_anchor_cache(state);
    }
}

glm::vec3 pin_anchor_position(const game_state& state) {
    if (static_anchor_cache_is_current(state)) {
        return state.static_anchors.pin_anchor;
    }
    return state.hole ? anchor_on_terrain(state.area, state.hole->pin_position) : glm::vec3(0.0f);
}
