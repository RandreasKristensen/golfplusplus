#include "game/game_state.h"

#include "game/course_session.h"
#include "game/progress_rules.h"
#include "physics/ball_physics.h"
#include "physics/collision.h"
#include "physics/ground_contact.h"
#include "physics/vector_math.h"
#include "physics/wind.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/trigonometric.hpp>

namespace {
// Longer frames are simulated as this long, so a hitch cannot tunnel the ball.
constexpr float max_update_seconds = 0.05f;
// A cart slower than this is parked (no movement XP, no drive sound).
constexpr float cart_parked_speed = 0.05f;

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
    club_stats stats = club != nullptr ? club->stats : club_stats{};
    if (state.cigarette_seconds_left > 0.0f) {
        stats.backspin *= state.rewards.cigarette.backspin_scale;
        stats.timing_speed *= state.rewards.cigarette.timing_speed_scale;
    }
    return stats;
}

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

void award_movement_xp(game_state& state, const movement_xp_rate& rate, float& pending_meters, const float meters) {
    if (meters <= 0.0f) {
        return;
    }
    const movement_xp_update update = award_movement_xp(state.save, rate, pending_meters, meters);
    pending_meters = update.remainder_meters;
    apply_progress_update(state, update.update);
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

void trigger_emote(emote_state& emote) {
    emote = emote_state{0.0f, true};
}

void tick_emote(emote_state& emote, const float duration, const float dt) {
    if (!emote.active) {
        return;
    }
    emote.elapsed += dt;
    if (emote.elapsed >= duration) {
        emote = emote_state{};
    }
}

void update_emotes(game_state& state, const game_input& input, const float dt) {
    if (input.smoke) {
        trigger_emote(state.smoke_emote);
        state.cigarette_seconds_left = state.rewards.cigarette.duration_seconds;
        award_skill_xp(state, state.rewards.smoke);
        push_audio_event(state, audio_event_type::emote_smoke);
    }
    if (input.drink) {
        trigger_emote(state.beer_emote);
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
    if (input.turn_left_held) {
        state.cart.yaw += turn_rate * dt;
    }
    if (input.turn_right_held) {
        state.cart.yaw -= turn_rate * dt;
    }

    const float target_speed = tuning.speed * (drifting ? tuning.drift_speed_boost : 1.0f) * road_speed;
    const float damping = (drifting ? tuning.drift_damping : tuning.normal_damping) * control;
    state.cart.velocity += (target_speed - state.cart.velocity) * (1.0f - std::exp(-std::max(0.0f, damping) * dt));

    const glm::vec3 before = state.player.position;
    state.player.position += yaw_direction(state.cart.yaw) * state.cart.velocity * dt;
    state.player.position.y = terrain_height(state.area, state.player.position, profile);
    state.player.yaw = state.cart.yaw;

    if (on_road && std::abs(state.cart.velocity) > cart_parked_speed) {
        const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
        award_movement_xp(state, state.rewards.cart_on_road, state.cart_meters_pending, meters);
        if (drifting) {
            award_movement_xp(state, state.rewards.drift_on_road, state.drift_meters_pending, meters);
        }
    }
}

void interact(game_state& state) {
    if (in_hub(state)) {
        if (const std::optional<std::size_t> collectible = nearby_collectible(state)) {
            const claim_update claim = claim_collectible(state.save, state.hub->world.collectibles[*collectible]);
            apply_progress_update(state, claim.update);
        } else if (const std::optional<std::size_t> start = nearby_hole_start(state)) {
            start_hub_hole(state, *start);
        }
        return;
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
    if (input.turn_left_held) {
        state.player.yaw += tuning.turn_rate * dt;
    }
    if (input.turn_right_held) {
        state.player.yaw -= tuning.turn_rate * dt;
    }

    const glm::vec3 before = state.player.position;
    const glm::vec3 forward = yaw_direction(state.player.yaw);
    if (input.forward_held) {
        state.player.position += forward * tuning.walk_speed * dt;
    }
    if (input.back_held) {
        state.player.position -= forward * tuning.walk_speed * dt;
    }
    state.player.position.y = terrain_height(state.area, state.player.position, profile);

    const float meters = horizontal_distance(state.player.position, before) * state.tuning.scale.meters_per_world_unit;
    award_movement_xp(state, state.rewards.walking, state.walk_meters_pending, meters);

    if (input.action) {
        interact(state);
    }
}

void change_club(game_state& state, const game_input& input) {
    if (state.clubs.empty() || (!input.previous_club && !input.next_club)) {
        return;
    }
    const std::size_t count = state.clubs.size();
    state.selected_club = input.previous_club ? (state.selected_club + count - 1) % count : (state.selected_club + 1) % count;
    push_audio_event(state, audio_event_type::club_change);
}

glm::vec3 address_position(const game_state& state, frame_profile* profile) {
    const glm::vec3 forward = yaw_direction(state.aim_angle);
    glm::vec3 position = state.ball.position
        + yaw_left(forward) * state.tuning.player.ball_stand_off_distance
        - forward * state.tuning.player.address_back_distance;
    position.y = terrain_height(state.area, position, profile);
    return position;
}

void update_aiming(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    if (input.turn_left_held) {
        state.aim_angle += state.tuning.player.aim_turn_rate * dt;
    }
    if (input.turn_right_held) {
        state.aim_angle -= state.tuning.player.aim_turn_rate * dt;
    }
    state.player.yaw = state.aim_angle;
    change_club(state, input);

    if (input.action) {
        state.player.position = address_position(state, profile);
        state.mode = game_mode::addressing;
        state.swing = swing_state{};
    }
}

void launch_ball(game_state& state) {
    const club_definition* club = selected_club(state);
    if (club == nullptr) {
        return;
    }

    const club_stats stats = effective_club_stats(state);
    const glm::vec3 forward = yaw_direction(state.aim_angle);
    const float loft = glm::radians(stats.loft_degrees);
    const glm::vec3 launch_direction = glm::normalize(forward * std::cos(loft) + world_up * std::sin(loft));
    const float speed = stats.power * std::max(state.tuning.swing.min_power, state.swing.power);

    state.shot_start_position = state.ball.position;
    state.ball.velocity = launch_direction * speed;
    // Backspin turns about the axis to the right of the shot (lift); side
    // spin turns about the shot direction itself (curve as the ball rises
    // and falls). Both follow the aim, so a shot flies the same either way.
    state.ball.spin = -yaw_left(forward) * (stats.backspin * speed)
        + forward * (stats.side_spin * state.tuning.swing.side_spin_scale);
    state.swing = swing_state{};
    state.mode = game_mode::following_shot;
    state.flight_path_points.clear();
    append_flight_path_point(state, state.ball.position);
    ++state.stroke_count;
    award_skill_xp(state, state.rewards.shot);

    audio_event hit;
    hit.type = audio_event_type::club_hit;
    hit.club_hit_sound = club->hit_sound;
    state.audio_events.push_back(hit);
}

void update_addressing(game_state& state, const game_input& input, const float dt) {
    change_club(state, input);

    if (state.swing.phase == swing_phase::timing) {
        state.swing.elapsed += dt;
        state.swing.power = sample_swing_power(state.swing.elapsed * effective_club_stats(state).timing_speed,
                                               state.tuning.swing.meter_cycle_seconds);
    }
    if (!input.action) {
        return;
    }
    if (state.swing.phase == swing_phase::idle) {
        state.swing = swing_state{swing_phase::timing, 0.0f, 0.0f};
        push_audio_event(state, audio_event_type::swing_start);
        return;
    }
    launch_ball(state);
}

void step_ball(game_state& state, const float dt, frame_profile* profile) {
    const ball_tuning& tuning = state.tuning.ball;
    const terrain_sample before = sample_area(state.area, state.ball.position, profile);
    if (!ball_is_moving(state)) {
        state.ball = resolve_terrain_collision(state.ball, before, tuning.ground_restitution, tuning.ground_friction, dt);
        state.ball.velocity = glm::vec3(0.0f);
        state.ball.spin = glm::vec3(0.0f);
        return;
    }

    const float roll_scale = std::max(0.0f, effective_club_stats(state).roll_friction_scale);
    const bool was_airborne = !ball_is_grounded(state.ball, before);
    const physics_tuning physics = ball_in_water(state.ball, before, state.tuning.terrain.zones.water_depth)
        ? with_water_drag(state.tuning.physics)
        : state.tuning.physics;
    const wind_state wind = sample_wind(state.area.wind_seed, state.hole_time, state.tuning.wind);
    state.ball = step_ball_flight(state.ball, wind, dt, physics);

    const terrain_sample after = sample_area(state.area, state.ball.position, profile, &before);
    const bool in_water = after.material == terrain_material::water;
    state.ball = resolve_terrain_collision(state.ball,
                                           after,
                                           in_water ? tuning.water_restitution : tuning.ground_restitution,
                                           in_water ? tuning.water_friction : tuning.ground_friction * roll_scale,
                                           dt);
    if (was_airborne && ball_is_grounded(state.ball, after)) {
        audio_event land;
        land.type = audio_event_type::ball_land;
        land.material = after.material;
        state.audio_events.push_back(land);
    }

    refresh_static_anchor_cache(state, profile);
    const ball_state before_trees = state.ball;
    state.ball = resolve_tree_collisions(state.ball, state.static_anchors.trees, tuning.tree_restitution, tuning.tree_friction);
    if (glm::length(state.ball.velocity - before_trees.velocity) > 0.01f ||
        glm::length(state.ball.position - before_trees.position) > 0.001f) {
        push_audio_event(state, audio_event_type::ball_tree_hit);
    }

    state.ball = apply_rolling_friction(state.ball, after, tuning.roll_deceleration * roll_scale, tuning.settle_speed, dt);
}

// How high above the cup the ball centre may pass and still drop in.
float cup_capture_height(const game_state& state) {
    return std::max(state.ball.radius * 4.0f, state.tuning.scale.ball_visual_radius_meters * 2.0f);
}

void sink_ball_and_complete_hole(game_state& state) {
    push_audio_event(state, audio_event_type::ball_cup);
    state.ball.position = pin_anchor_position(state) - glm::vec3(0.0f, state.ball.radius * 2.0f, 0.0f);
    state.ball.velocity = glm::vec3(0.0f);
    state.ball.spin = glm::vec3(0.0f);
    complete_current_hole(state);
}

// Steps a moving ball; returns true when it dropped into the cup.
bool update_ball(game_state& state, const float dt, frame_profile* profile) {
    if (state.mode != game_mode::following_shot && !ball_is_moving(state)) {
        return state.mode == game_mode::walking && ball_is_in_cup(state);
    }

    state.mode = game_mode::following_shot;
    const glm::vec3 previous = state.ball.position;
    step_ball(state, dt, profile);
    append_flight_path_point(state, state.ball.position);
    if (path_crosses_cup(previous, state.ball.position, pin_anchor_position(state),
                         state.tuning.scale.cup_radius_meters, cup_capture_height(state))) {
        return true;
    }
    if (!ball_is_moving(state)) {
        state.ball.velocity = glm::vec3(0.0f);
        state.ball.spin = glm::vec3(0.0f);
        state.mode = game_mode::walking;
        state.flight_path_points.clear();
    }
    return false;
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
    if (round_finished(state.round)) {
        return;
    }
    const float dt = std::clamp(raw_dt, 0.0f, max_update_seconds);
    refresh_static_anchor_cache(state, profile);
    update_xp_drops(state, dt);
    state.hole_time += dt;
    update_emotes(state, input, dt);

    if (input.retee && state.hole) {
        retee_ball(state);
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
        if (state.hole && update_ball(state, dt, profile)) {
            sink_ball_and_complete_hole(state);
        }
    }

    update_overlays(state, input);
}

void award_skill_xp(game_state& state, const xp_reward& reward) {
    apply_progress_update(state, award_xp(state.save, reward));
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

bool ball_is_moving(const game_state& state) {
    if (!state.hole) {
        return false;
    }
    return glm::length(state.ball.velocity) > state.tuning.ball.stop_speed ||
        !ball_is_grounded(state.ball, sample_area(state.area, state.ball.position));
}

bool ball_is_in_cup(const game_state& state) {
    return state.hole && horizontal_distance(state.ball.position, pin_anchor_position(state)) <= state.tuning.scale.cup_radius_meters;
}

bool can_interact_with_ball(const game_state& state) {
    return state.hole &&
        horizontal_distance(state.player.position, state.ball.position) <= state.tuning.player.ball_interact_radius &&
        !ball_is_moving(state);
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
            collectible_available(state.save, collectibles[i])) {
            best = i;
            best_distance = distance;
        }
    }
    return best;
}

bool cart_on_road(const game_state& state) {
    if (!in_hub(state)) {
        return false;
    }
    const cart_tuning& tuning = state.tuning.cart;
    const glm::vec3 position = horizontal(state.player.position);
    for (const course_world_cart_road& road : state.hub->world.cart_roads) {
        const float reach = std::max(tuning.min_road_reach, road.width * 0.5f + tuning.road_reach_margin);
        for (std::size_t i = 0; i + 1 < road.polyline.size(); ++i) {
            const glm::vec3 a = horizontal(road.polyline[i]);
            const glm::vec3 ab = horizontal(road.polyline[i + 1]) - a;
            const float length_squared = glm::dot(ab, ab);
            const float t = length_squared <= 0.0001f ? 0.0f : clamp01(glm::dot(position - a, ab) / length_squared);
            if (glm::length(position - (a + ab * t)) <= reach) {
                return true;
            }
        }
    }
    return false;
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
    cache.trees.reserve(area.trees.size());
    for (const tree_instance& tree : area.trees) {
        cache.trees.push_back(tree_body{anchor_on_terrain(area, tree.position, profile), tree.shape});
    }
    if (in_hub(state)) {
        for (const hub_hole_marker& marker : state.hub->markers) {
            cache.hub_tee_markers.push_back(anchor_on_terrain(area, marker.tee_position, profile));
            cache.hub_pin_markers.push_back(anchor_on_terrain(area, marker.pin_position, profile));
            cache.hub_start_markers.push_back(anchor_on_terrain(area, marker.start_position, profile));
        }
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
