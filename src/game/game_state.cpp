#include "game/game_state.h"

#include "game/course_session.h"
#include "game/group_round.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/online_play.h"
#include "game/player_jump.h"
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
    state.rangefinder_active = walking && on_foot(state) && input.rangefinder_held && target.has_value();
    state.rangefinder_distance_meters = target
        ? horizontal_distance(state.player.position, *target) * state.tuning.scale.meters_per_world_unit
        : 0.0f;
    state.course_map_active = walking && input.course_map_held;
    state.scorecard_active = walking && input.scorecard_held;
    state.skills_panel_active = input.skills_panel_held;
}

// A hole start is its tee: the press that starts the hole also takes up the
// ball teed there, like any press beside the ball. A ball left in its cup is
// picked up first: no hole starts before then. A cart's seat in reach is
// stepped into unless the hole start or the ball is nearer.
void interact(game_state& state) {
    const std::optional<reachable_seat> seat = nearest_seat(state);
    const auto seat_nearer_than = [&seat](const float distance) { return seat && seat->distance < distance; };
    if (in_hub(state)) {
        const bool cup_ball = cup_ball_in_reach(state);
        const std::optional<std::size_t> collectible = nearby_collectible(state);
        const std::optional<std::size_t> start = nearby_hole_start(state);
        const bool to_seat = seat && !cup_ball && !collectible &&
            (!start || seat_nearer_than(horizontal_distance(state.player.position, state.hub->world.hole_starts[*start].position)));
        if (to_seat) {
            step_into_seat(state, *seat);
            return;
        }
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
    } else if (seat && (!can_interact_with_ball(state) ||
                        seat_nearer_than(horizontal_distance(state.player.position, state.ball.position)))) {
        step_into_seat(state, *seat);
        return;
    }
    if (can_interact_with_ball(state)) {
        state.mode = game_mode::aiming;
        state.aim_angle = state.player.yaw;
        state.swing = swing_state{};
    }
}

// The arrow keys and the mouse: on foot they turn me, in a cart my head.
void look_around(game_state& state, const game_input& input, const float dt) {
    const player_tuning& tuning = state.tuning.player;
    const float turn = tuning.turn_rate * turn_direction(input);
    if (state.ride) {
        state.look.head_yaw = wrap_angle(state.look.head_yaw + turn * dt + input.look_yaw);
    } else {
        state.player.turn_rate = turn;
        state.player.yaw = wrap_angle(state.player.yaw + turn * dt + input.look_yaw);
    }
    const float pitch = state.look.pitch + tuning.look_pitch_rate * look_pitch_direction(input) * dt + input.look_pitch;
    state.look.pitch = std::clamp(pitch, -tuning.max_look_pitch, tuning.max_look_pitch);
}

// W and S walk where I look, A and D step sideways, at walking speed in any
// direction. Moving backwards I keep facing forward: the speed is negative
// and the move angle within a quarter turn of my yaw.
void walk(game_state& state, const game_input& input, const float dt) {
    const float forward = walk_direction(input);
    const float side = strafe_direction(input);
    state.player.move_angle = 0.0f;
    if (forward == 0.0f && side == 0.0f) {
        return;
    }
    const float sign = forward < 0.0f ? -1.0f : 1.0f;
    state.player.move_angle = std::atan2(side * sign, forward * sign);
    state.player.speed = state.tuning.player.walk_speed * sign;
    state.player.position += yaw_direction(state.player.yaw + state.player.move_angle) * (state.player.speed * dt);
}

// Space hops from the ground, and I walk on in the air as on the ground.
// Online, the frame I rise past airborne_height tells others I hopped.
void jump(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    const player_tuning& tuning = state.tuning.player;
    std::optional<float>& seconds = state.player.hop_elapsed;
    const float before = hop_height(seconds.value_or(0.0f), tuning);
    if (!seconds && input.jump) {
        seconds = 0.0f;
    }
    if (seconds) {
        *seconds += dt;
        if (*seconds >= hop_seconds(tuning)) {
            seconds.reset();
        }
    }
    const float height = hop_height(seconds.value_or(0.0f), tuning);
    state.player.position.y = terrain_height(state.area, state.player.position, profile) + height;
    if (before <= airborne_height && height > airborne_height && !state.ball_correction) {
        send_motion_now(state);
    }
}

void update_walking(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    look_around(state, input, dt);
    if (state.ride) {
        update_cart(state, input, dt, profile);
        return;
    }

    const glm::vec3 before = state.player.position;
    walk(state, input, dt);
    jump(state, input, dt, profile);

    const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
    record_movement(state, state.rewards.walking, state.walk_meters_pending, meters);

    if (input.interact) {
        interact(state);
    }
    // Taking up the ball or a seat puts me back on my feet.
    if (state.player.hop_elapsed && (state.mode != game_mode::walking || state.ride)) {
        state.player.hop_elapsed.reset();
        if (!state.ride) {
            state.player.position.y = terrain_height(state.area, state.player.position, profile);
        }
    }
}

// Round the bag either way, one club a step.
void change_club(game_state& state, const game_input& input) {
    if (state.clubs.empty() || input.club_steps == 0) {
        return;
    }
    const int count = static_cast<int>(state.clubs.size());
    const int index = (static_cast<int>(state.selected_club) + input.club_steps % count + count) % count;
    state.selected_club = static_cast<std::size_t>(index);
    push_audio_event(state, audio_event_type::club_change);
}

// The arrow keys, A and D and the mouse all turn the aim; E steps up to the
// ball.
void update_aiming(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    const float turn = std::clamp(turn_direction(input) + strafe_direction(input), -1.0f, 1.0f);
    state.player.turn_rate = state.tuning.player.aim_turn_rate * turn;
    state.aim_angle = wrap_angle(state.aim_angle + state.player.turn_rate * dt + input.look_yaw);
    state.player.yaw = state.aim_angle;
    change_club(state, input);

    if (input.interact) {
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
    if (state.swing.phase == swing_phase::idle) {
        if (!input.backswing) {
            return;
        }
        state.swing = swing_state{swing_phase::timing, 0.0f, 0.0f};
        push_audio_event(state, audio_event_type::swing_start);
        // Online others raise their club with mine from when the server hears it.
        send_motion_now(state);
        return;
    }
    if (input.downswing) {
        launch_shot(state);
    }
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
        // The shot camera was watching the ball drop: walk on looking at the cup.
        state.player.yaw = yaw_towards(state.player.position, state.ball.position);
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
        // Online, my results wait for the group; the room plays on meanwhile,
        // and I can walk and drive around it, but play nothing more.
        if (waiting_for_group(state)) {
            state.player.speed = 0.0f;
            state.player.turn_rate = 0.0f;
            state.player.move_angle = 0.0f;
            update_emotes(state, input, dt);
            game_input moving = input;
            moving.interact = false;
            update_walking(state, moving, dt, profile);
            watch_room(state, dt);
            update_remote_players(state, dt);
            sync_motion(state, dt);
        }
        return;
    }
    refresh_static_anchor_cache(state, profile);
    update_xp_drops(state, dt);
    state.hole_time += dt;
    state.player.speed = 0.0f;
    state.player.turn_rate = 0.0f;
    state.player.move_angle = 0.0f;
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
        if (state.mode != game_mode::walking) {
            leave_cart_at_once(state);
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

float view_yaw(const game_state& state) {
    return wrap_angle(state.player.yaw + (state.ride ? state.look.head_yaw : 0.0f));
}

bool in_hub(const game_state& state) {
    return state.hub.has_value() && !state.hole.has_value();
}

int local_zone(const game_state& state) {
    // A finished round keeps its last hole for the results, but holing out
    // put me back in the hub.
    return state.hole && !round_finished(state.round) ? static_cast<int>(state.hole->index) : hub_zone;
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
