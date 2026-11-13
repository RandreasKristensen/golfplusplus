#include "game/game_state.h"

#include "game/address_play.h"
#include "game/bag_handling.h"

#include "game/course_session.h"
#include "game/golf_bag.h"
#include "game/green_read.h"
#include "game/group_round.h"
#include "game/interactables.h"
#include "game/social_dap.h"
#include "game/hub_flow.h"
#include "game/hub_session.h"
#include "game/hub_ball.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/online_play.h"
#include "game/progress_rules.h"
#include "game/rangefinder.h"
#include "game/remote_players.h"
#include "game/ride_sync.h"
#include "game/shot_simulation.h"
#include "game/text_ids.h"
#include "game/walking.h"
#include "game/world_sounds.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/geometric.hpp>

namespace {
// Longer frames are played as this long, so a hitch does not jump the player
// or a playing shot far ahead.
constexpr float max_update_seconds = 0.05f;

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

// Using what I smoke or drink: a use plays for its item's seconds and does
// not start again meanwhile, online or off (the server refuses it too).
void use_item(game_state& state, std::optional<item_use_shown>& shown, const std::string& item_id) {
    const item_definition* item = state.items.find(item_id);
    if (item == nullptr || !item->use || shown) {
        return;
    }
    shown = item_use_shown{item->id, 0.0f};
    record_item_use(state, *item);
    push_item_use(state, *item, state.player.position);
}

// Any time, waiting on a rolling ball too.
void update_item_uses(game_state& state, const game_input& input, const float dt) {
    const loadout equipped = equipped_loadout(state);
    if (input.use_smokable && !dap_holds_movement(state)) {
        use_item(state, state.smoking, equipped.smokable);
    }
    if (input.use_drinkable && !dap_holds_movement(state)) {
        use_item(state, state.drinking, equipped.drinkable);
    }
    tick_item_use(state.smoking, state.items, dt);
    tick_item_use(state.drinking, state.items, dt);
}

void update_overlays(game_state& state, const game_input& input, const float dt) {
    const bool walking = state.mode == game_mode::walking;
    update_rangefinder(state, input, dt);
    update_crouch(state, dap_holds_movement(state) ? game_input{} : input, dt);
    state.course_map_active = walking && input.course_map_held;
    state.scorecard_active = walking && input.scorecard_held && state.round.has_value();
    state.skills_panel_active = input.skills_panel_held;
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
    my_ball& ball = *state.ball;
    ball.position = shot_position_at(shot.result, shot.elapsed) + shot.correction * progress;
    shot.next_event = push_shot_events(state, shot.result, shot.correction, shot.next_event, shot.elapsed);
    append_flight_path_point(state, ball.position);
    const bool following = state.mode == game_mode::following_shot;
    if (following && shot.elapsed >= shot.follow_seconds && shot.elapsed < shot.result.duration) {
        // Rolling out: I walk again, looking at my ball, which plays on.
        state.mode = game_mode::walking;
        state.player.yaw = yaw_towards(state.player.position, ball.position);
        state.flight_path_points.clear();
    }
    if (shot.elapsed < shot.result.duration) {
        return;
    }

    const bool holed = shot.result.holed;
    // Lost in water, the ball is back where it was hit from, a stroke later;
    // a ball hit on the green costs two.
    ball.strokes += shot.result.penalty_strokes;
    if (shot.result.penalty_strokes > 0) {
        state.notice = game_notice{shot.result.lost ? text_hud_water_penalty : text_hud_ball_hit_penalty, 0.0f};
    }
    state.shot.reset();
    state.mode = game_mode::walking;
    state.flight_path_points.clear();
    if (holed && following) {
        // The shot camera was watching the ball drop: walk on looking at the cup.
        state.player.yaw = yaw_towards(state.player.position, ball.position);
    }
    if (holed) {
        push_audio_event(state, audio_event_type::ball_cup, ball.position);
    }
    // Online the hole is over when the server says so (game/online_play.h).
    if (holed && !is_online(state)) {
        if (state.hub) { finish_practice_shot(state); }
        else { complete_current_hole(state); }
    }
}
}

void tick_item_use(std::optional<item_use_shown>& use, const item_catalog& items, const float dt) {
    if (!use) {
        return;
    }
    use->elapsed += dt;
    const item_definition* item = items.find(use->item);
    if (item == nullptr || !item->use || use->elapsed >= item->use->seconds_per_use) {
        use.reset();
    }
}

game_state make_game_state(const game_content& content, const save_data& save) {
    game_state state;
    state.asset_root = content.asset_root;
    state.hub_props = content.hub_props;
    state.hub_sky = content.hub.sky;
    state.tuning = content.tuning;
    state.clubs = content.clubs;
    state.rewards = content.rewards;
    state.skills = content.skills;
    state.items = content.items;
    state.default_loadout = content.default_loadout;
    state.save = save;
    state.save.equipped = fitted_loadout(save.equipped, content.default_loadout, content.items);
    return state;
}

void update_game(game_state& state, const game_input& input, const float raw_dt, frame_profile* profile) {
    const float dt = std::clamp(raw_dt, 0.0f, max_update_seconds);
    if (is_online(state) && ((state.online.room && !state.online.room_ready) || !state.hub_flow.travel_course.empty())) { return; }
    if (state.hub_flow.screen != hub_screen::none || state.hub_flow.ball_request) {
        update_online_play(state, dt);
        if (!state.hub_flow.ball_request) { sync_motion(state, dt); }
        return;
    }
    if ((state.round && round_finished(*state.round)) && !state.results_dismissed) {
        // Online, my results wait for the group; the room plays on meanwhile,
        // and I can walk and drive around it, but play nothing more.
        if (waiting_for_group(state)) {
            state.player.speed = 0.0f;
            state.player.turn_rate = 0.0f;
            state.player.move_angle = 0.0f;
            update_item_uses(state, input, dt);
            game_input moving = input;
            moving.interact = input.interact;
            update_walking(state, moving, dt, profile);
            watch_room(state, dt);
            update_remote_players(state, dt);
            sync_motion(state, dt);
        } else {
            watch_room(state, dt);
            update_remote_players(state, dt);
        }
        return;
    }
    refresh_static_anchor_cache(state, profile);
    update_xp_drops(state, dt);
    update_shot_number(state, dt);
    update_pace_count(state, dt);
    state.weather = course_weather_at(state, shot_unix_ms(state));
    state.player.speed = 0.0f;
    state.player.turn_rate = 0.0f;
    state.player.move_angle = 0.0f;
    update_item_uses(state, input, dt);
    follow_ride(state, dt);
    if (input.leave_group) {
        request_leave_group(state);
    }
    // Whatever I am doing: the hub's balls are for fooling about.
    if (input.drop_practice_ball) {
        request_practice_drop(state);
    }

    if (input.cancel && state.give_up_prompt) {
        state.give_up_prompt.reset();
    } else if (input.cancel && state.mode == game_mode::addressing && !state.address.swing_elapsed &&
               !state.address.meter_elapsed) {
        state.mode = game_mode::walking;
    } else {
        switch (state.mode) {
        case game_mode::walking:
            update_walking(state, input, dt, profile);
            break;
        case game_mode::addressing:
            update_addressing(state, input, dt, profile);
            break;
        case game_mode::following_shot:
            break;
        }
        if (state.mode != game_mode::walking) {
            leave_cart_at_once(state);
        }
        update_shot_playback(state, dt);
    }
    update_carts(state, dt);
    update_bag_handling(state, dt);

    update_overlays(state, input, dt);
    update_online_play(state, dt);
    update_remote_players(state, dt);
    if (!state.hub_flow.ball_request) { sync_motion(state, dt); }
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

void update_shot_number(game_state& state, const float dt) {
    if (state.shot_number) {
        state.shot_number->age += std::max(0.0f, dt);
        if (state.shot_number->age >= state.tuning.shot_numbers.lifetime_seconds) {
            state.shot_number.reset();
        }
    }
}

float view_yaw(const game_state& state) {
    return wrap_angle(state.player.yaw + (state.ride ? state.look.head_yaw : 0.0f));
}

bool course_started(const game_state& state) {
    return state.kind == place_kind::hub ? state.hub.has_value() : !state.holes.empty();
}

bool ball_in_play(const game_state& state) {
    return state.ball && state.ball->place != ball_place::in_cup;
}

bool shot_playing(const game_state& state) {
    return state.shot.has_value();
}

std::optional<std::size_t> flag_in_reach(const game_state& state) {
    if (sample_area(state.area, state.player.position).material != terrain_material::green) {
        return std::nullopt;
    }
    const std::vector<cup>& cups = state.area.cups;
    for (std::size_t i = 0; i < cups.size() && i < state.flags_in.size(); ++i) {
        if (horizontal_distance(state.player.position, cups[i].centre) <= state.tuning.player.flag_reach) {
            return i;
        }
    }
    return std::nullopt;
}

void toggle_flag_in_reach(game_state& state) {
    if (const std::optional<std::size_t> cup = flag_in_reach(state)) {
        record_flag(state, *cup, state.flags_in[*cup]);
    }
}

shot_course current_shot_course(const game_state& state) {
    auto course = make_shot_course(state.area, state.ball->hole.value_or(state.area.cups.size()), state.weather, state.place, state.flags_in, state.tuning);
    course.practice = state.kind == place_kind::hub;
    const float speed = sample_area(state.area, state.ball->position).green_stimp_feet;
    if (speed > 0) { course.green_stimp_feet = speed; }
    return course;
}

void play_shot(game_state& state, shot_result result) {
    my_ball& ball = *state.ball;
    if (!result.trajectory.empty()) {
        ball.position = result.trajectory.front();
    }
    state.shot_start_position = ball.position;
    state.mode = game_mode::following_shot;
    state.flight_path_points.clear();
    append_flight_path_point(state, ball.position);
    const float follow_seconds =
        shot_rolled_at(result, state.tuning.camera.follow_roll_meters / state.tuning.scale.meters_per_world_unit);
    state.shot = shot_playback{std::move(result), 0.0f, follow_seconds, 0};
}

bool interact_available(const game_state& state) {
    return state.mode == game_mode::walking && on_foot(state) && !state.give_up_prompt && nearest_interactable(state);
}

bool cart_on_road(const game_state& state) {
    return on_cart_road(state.world.cart_roads, state.player.position, state.tuning.cart);
}

static_anchor_cache build_static_anchor_cache(const game_state& state, frame_profile* profile) {
    static_anchor_cache cache;
    cache.valid = true;
    cache.revision = state.terrain_render_revision;
    const play_area& area = state.area;
    cache.trees = standing_trees(area, profile);
    for (const course_hole& hole : state.holes) {
        cache.tees.push_back(anchor_on_terrain(area, hole.tee_position, profile));
        cache.pins.push_back(anchor_on_terrain(area, hole.pin_position, profile));
    }
    if (state.hub) {
        for (const auto& cup : state.area.cups) { cache.pins.push_back(cup.centre); }
        for (const auto& target : state.hub->targets) { cache.pins.push_back(anchor_on_terrain(area, target.position)); }
    }
    for (const course_world_collectible& collectible : state.world.collectibles) {
        cache.collectibles.push_back(anchor_on_terrain(area, collectible.position, profile));
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

glm::vec3 tee_anchor_position(const game_state& state, const std::size_t hole) {
    if (static_anchor_cache_is_current(state) && hole < state.static_anchors.tees.size()) {
        return state.static_anchors.tees[hole];
    }
    return hole < state.holes.size() ? anchor_on_terrain(state.area, state.holes[hole].tee_position) : glm::vec3(0.0f);
}

glm::vec3 pin_anchor_position(const game_state& state, const std::size_t hole) {
    if (static_anchor_cache_is_current(state) && hole < state.static_anchors.pins.size()) {
        return state.static_anchors.pins[hole];
    }
    return hole < state.holes.size() ? anchor_on_terrain(state.area, state.holes[hole].pin_position) : glm::vec3(0.0f);
}
