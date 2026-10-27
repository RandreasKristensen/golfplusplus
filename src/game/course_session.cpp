#include "game/course_session.h"

#include "game/content_files.h"
#include "game/golf_bag.h"
#include "game/mode_dispatch.h"
#include "game/net_types.h"
#include "game/shot_dice.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include <utility>

namespace {
void face(game_state& state, const float yaw) {
    state.player.yaw = yaw;
    state.look.pitch = 0.0f;
}

// What lives for one ball's hole: its shot and the address over it.
void end_ball_play(game_state& state) {
    state.shot.reset();
    state.mode = game_mode::walking;
    state.address = address_state{};
    state.flight_path_points.clear();
    state.online_hole = online_hole_state{};
    state.ball_correction.reset();
    state.give_up_prompt.reset();
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

std::vector<course_hole> place_holes(const course_world_definition& world, const std::vector<hole_data>& holes) {
    std::vector<course_hole> placed;
    for (std::size_t i = 0; i < holes.size(); ++i) {
        const hole_data hole = place_hole(holes[i], world.hole_starts[i]);
        placed.push_back(course_hole{hole.tee_position, hole.pin_position, hole.par});
    }
    return placed;
}
}

bool start_course(game_state& state, const course_definition& course) {
    std::optional<std::vector<hole_data>> holes = load_course_holes(state.asset_root, course);
    if (!holes || holes->empty()) {
        return false;
    }

    const std::optional<course_world_definition> world =
        load_course_world_from_file(course_world_file_path(state.asset_root, course), course);
    if (!world) {
        return false;
    }

    state.course = course;
    state.area = build_course_area(*holes, *world, state.tuning);
    state.world = *world;
    state.holes = place_holes(*world, *holes);
    state.course_holes = std::move(*holes);
    state.flags_in.assign(state.course_holes.size(), true);
    mark_terrain_render_dirty(state);
    state.round = start_round(state.course_holes.size());
    state.round_seed = round_shot_seed(static_cast<std::uint64_t>(state.local_unix_ms), 0);
    state.shots_hit = 0;
    state.picker.reset();
    state.server_bag_adopted = false;
    state.xp_drops.clear();
    state.pending_xp_drop_amounts.clear();
    state.ball.reset();
    end_ball_play(state);
    leave_cart_at_once(state);
    state.smoking.reset();
    state.drinking.reset();
    state.walk_meters_pending = 0.0f;
    state.cart_meters_pending = 0.0f;
    state.shot_start_position = glm::vec3(0.0f);
    state.player.position = anchor_on_terrain(state.area, state.world.hole_starts.front().position);
    face(state, yaw_towards(state.player.position, state.holes.front().pin_position));
    park_cart(state);
    strap_bag_on_cart(state);
    return true;
}

bool tee_up(game_state& state, const std::size_t hole_index, const bool give_up) {
    if (hole_index >= state.holes.size() || hole_played(state.round, hole_index)) {
        return false;
    }
    if (state.ball && state.ball->place == ball_place::in_cup) {
        state.notice = game_notice{text_ball_in_cup, 0.0f};
        return false;
    }
    if (state.ball && state.ball->hole == hole_index) {
        return false;  // that hole's ball is mine: it is played where it lies
    }
    if (state.ball && !give_up) {
        state.notice = game_notice{text_ball_in_play, 0.0f};
        return false;
    }
    if (is_online(state) &&
        tee_in_use(state.online.balls, state.online.account_id, static_cast<int>(hole_index))) {
        state.notice = game_notice{text_online_tee_in_use, 0.0f};
        return false;
    }
    end_ball_play(state);
    state.round.current_hole_index = hole_index;
    const float radius = state.tuning.scale.ball_physics_radius_meters;
    const glm::vec3 tee = resting_ball_position(state.area, state.holes[hole_index].tee_position, radius);
    state.ball = my_ball{hole_index, tee, 0, ball_place::teed};
    const glm::vec3 pin = pin_anchor_position(state, hole_index);
    face(state, yaw_towards(tee, pin));
    state.player.position = tee_stance_position(state.area, tee, pin, state.tuning.player.ball_stand_off_distance);
    record_tee_up(state, hole_index, give_up);
    return true;
}

void complete_current_hole(game_state& state) {
    if (!ball_in_play(state)) {
        return;
    }
    my_ball& ball = *state.ball;
    state.round = complete_hole(state.round, ball.hole, ball.strokes);
    record_hole_completed(state);
    end_ball_play(state);
    // The ball stays in the cup until it is picked up, into the next round too.
    ball.place = ball_place::in_cup;
    ball.position = pin_anchor_position(state, ball.hole);
}

void give_up_hole(game_state& state) {
    if (!ball_in_play(state)) {
        return;
    }
    end_ball_play(state);
    state.ball.reset();
    record_hole_given_up(state);
}

void start_next_round(game_state& state) {
    if (!course_started(state) || !round_finished(state.round)) {
        return;
    }
    state.round = start_round(state.course_holes.size());
    state.group_strokes.clear();
}

bool cup_ball_in_reach(const game_state& state) {
    return state.ball && state.ball->place == ball_place::in_cup &&
        horizontal_distance(state.player.position, state.ball->position) <= state.tuning.player.ball_interact_radius;
}

bool pick_up_cup_ball(game_state& state) {
    if (!cup_ball_in_reach(state)) {
        return false;
    }
    state.ball.reset();
    record_ball_picked_up(state);
    return true;
}
