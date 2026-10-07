#include "game/course_session.h"

#include "game/content_files.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <utility>

namespace {
// Clears everything that only lives for one hole or one visit to the hub.
void reset_play_state(game_state& state) {
    state.ball = ball_state{};
    state.ball.radius = state.tuning.scale.ball_physics_radius_meters;
    state.shot.reset();
    state.cart = cart_state{};
    state.smoke_emote = emote_state{};
    state.beer_emote = emote_state{};
    state.cigarette_seconds_left = 0.0f;
    state.mode = game_mode::walking;
    state.swing = swing_state{};
    state.stroke_count = 0;
    state.hole_time = 0.0f;
    state.walk_meters_pending = 0.0f;
    state.cart_meters_pending = 0.0f;
    state.drift_meters_pending = 0.0f;
    state.flight_path_points.clear();
    state.shot_start_position = glm::vec3(0.0f);
    state.online_hole = online_hole_state{};
    state.ball_correction.reset();
}

void face(game_state& state, const float yaw) {
    state.player.yaw = yaw;
    state.cart.yaw = yaw;
    state.aim_angle = yaw;
}

// Ball resting on the tee, player standing behind it facing the pin.
void tee_up(game_state& state) {
    state.ball.position = resting_ball_position(state.area, state.hole->tee_position, state.ball.radius);
    const glm::vec3 pin = pin_anchor_position(state);
    face(state, yaw_towards(state.ball.position, pin));
    state.player.position =
        tee_stance_position(state.area, state.ball.position, pin, state.tuning.player.ball_stand_off_distance);
}

// `placed_hole` is in the play area's coordinates.
void enter_hole(game_state& state, const std::size_t index, const hole_data& placed_hole) {
    state.hole = active_hole{index, placed_hole.tee_position, placed_hole.pin_position, placed_hole.wind_seed};
    mark_terrain_render_dirty(state);
    reset_play_state(state);
    tee_up(state);
    record_hole_started(state, index);
}

// Faces the start of the next hole to play, or down that hole when already
// standing at its start.
float hub_facing_yaw(const game_state& state) {
    const std::size_t next = state.round.current_hole_index;
    const course_world_hole_start& start = state.hub->world.hole_starts[next];
    if (horizontal_distance(state.player.position, start.position) <= start.interaction_radius) {
        return yaw_towards(state.player.position, state.hub->markers[next].pin_position);
    }
    return yaw_towards(state.player.position, start.position);
}

void enter_hub(game_state& state, const glm::vec3& position) {
    state.hole.reset();
    mark_terrain_render_dirty(state);
    reset_play_state(state);
    state.player.position = anchor_on_terrain(state.area, position);
    state.ball.position = state.player.position;
    face(state, hub_facing_yaw(state));
}

std::optional<std::vector<hole_data>> load_course_holes(const std::string& asset_root, const course_definition& course) {
    std::vector<hole_data> holes;
    for (std::size_t i = 0; i < course.holes.size(); ++i) {
        std::optional<hole_data> hole = load_hole_from_file(course_hole_path(asset_root, course, i));
        if (!hole) {
            return std::nullopt;
        }
        holes.push_back(std::move(*hole));
    }
    return holes;
}

course_hub build_hub(const course_world_definition& world, const std::vector<hole_data>& holes) {
    course_hub hub;
    hub.world = world;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        const course_world_hole_start& start = world.hole_starts[i];
        const hole_data placed = place_hole(holes[i], start);
        hub.markers.push_back(hub_hole_marker{placed.tee_position, placed.pin_position, start.position, placed.wind_seed});
    }
    return hub;
}

void enter_linear_hole(game_state& state, const std::size_t index) {
    const hole_data& hole = state.course_holes[index];
    state.area = build_hole_area(hole, state.tuning);
    enter_hole(state, index, hole);
}
}

bool start_course(game_state& state, const course_definition& course) {
    std::optional<std::vector<hole_data>> holes = load_course_holes(state.asset_root, course);
    if (!holes || holes->empty()) {
        return false;
    }

    std::optional<course_hub> hub;
    play_area course_area;
    if (!course.world.empty()) {
        const std::optional<course_world_definition> world =
            load_course_world_from_file(course_world_file_path(state.asset_root, course), course);
        if (!world) {
            return false;
        }
        hub = build_hub(*world, *holes);
        course_area = build_course_area(*holes, *world, state.tuning);
    }

    state.course = course;
    state.course_holes = std::move(*holes);
    state.round = start_round(state.course_holes.size());
    state.hub = std::move(hub);
    state.xp_drops.clear();
    state.pending_xp_drop_amounts.clear();
    state.cup_ball.reset();
    state.picked_cup_ball.reset();
    if (state.hub) {
        state.area = std::move(course_area);
        enter_hub(state, state.hub->world.hole_starts.front().position);
    } else {
        enter_linear_hole(state, 0);
    }
    return true;
}

bool start_hub_hole(game_state& state, const std::size_t hole_index) {
    if (!in_hub(state) || hole_index >= state.course_holes.size() || hole_played(state.round, hole_index)) {
        return false;
    }
    if (state.cup_ball) {
        state.notice = game_notice{text_ball_in_cup, 0.0f};
        return false;
    }
    if (is_online(state) &&
        tee_in_use(state.online.balls, state.online.account_id, static_cast<int>(hole_index))) {
        state.notice = game_notice{text_online_tee_in_use, 0.0f};
        return false;
    }
    const hole_data& hole = state.course_holes[hole_index];
    const course_world_hole_start& start = state.hub->world.hole_starts[hole_index];
    state.round.current_hole_index = hole_index;
    enter_hole(state, hole_index, place_hole(hole, start));
    return true;
}

void complete_current_hole(game_state& state) {
    if (!state.hole) {
        return;
    }
    const std::size_t index = state.hole->index;
    state.round = complete_hole(state.round, index, state.stroke_count);
    record_hole_completed(state);
    state.mode = game_mode::walking;
    state.flight_path_points.clear();
    // The ball stays in the cup on the course until it is picked up, into
    // the next round too; a course without a hub moves on to another area.
    if (state.hub) {
        state.cup_ball = pin_anchor_position(state);
    }

    if (round_finished(state.round)) {
        return;
    }
    // The course is one place: the player stays where they holed out from.
    if (state.hub) {
        enter_hub(state, state.player.position);
    } else {
        enter_linear_hole(state, state.round.current_hole_index);
    }
}

void abandon_hole(game_state& state, const glm::vec3& position) {
    if (state.hole && state.hub) {
        enter_hub(state, position);
    }
}

void start_next_round(game_state& state) {
    if (!state.hub || !state.hole) {
        return;
    }
    state.round = start_round(state.course_holes.size());
    state.group_strokes.clear();
    enter_hub(state, state.player.position);
}

std::size_t course_hole_of_area_hole(const game_state& state, const std::size_t area_hole) {
    if (!state.hub && state.hole) {
        return state.hole->index;
    }
    return area_hole;
}

void retee_ball(game_state& state) {
    if (!state.hole) {
        return;
    }
    const int strokes = state.stroke_count;
    reset_play_state(state);
    state.stroke_count = strokes;
    tee_up(state);
}

bool cup_ball_in_reach(const game_state& state) {
    return in_hub(state) && state.cup_ball &&
        horizontal_distance(state.player.position, *state.cup_ball) <= state.tuning.player.ball_interact_radius;
}

bool pick_up_cup_ball(game_state& state) {
    if (!cup_ball_in_reach(state)) {
        return false;
    }
    state.picked_cup_ball = state.cup_ball;
    state.cup_ball.reset();
    record_ball_picked_up(state);
    return true;
}
