#include "game/hub_session.h"
#include "game/content_files.h"
#include "game/course_world_loader.h"
#include "game/golf_bag.h"
#include "game/hub_flow.h"
#include "game/hub_scene.h"
#include "game/mode_dispatch.h"
#include "physics/vector_math.h"

bool start_hub(game_state& state, const hub_definition& hub) {
    const auto text = read_text_file(state.asset_root + "/" + hub.world);
    auto world = text ? parse_world_from_text(*text) : std::nullopt;
    if (!world) { return false; }
    world->hub = hub.anchors;
    state.kind = place_kind::hub;
    state.place = hub;
    state.hub = hub;
    state.round.reset();
    state.course_holes.clear(); state.holes.clear();
    state.world = *world;
    state.area = build_hub_area(hub, *world, state.tuning, bus_ground_pad(*world, state.hub_props));
    place_hub_scene(state.area, *world, state.hub_props, state.tuning);
    state.flags_in.assign(state.area.cups.size(), true);
    state.ball.reset(); state.shot.reset(); state.address = address_state{};
    state.online_hole = online_hole_state{}; state.ball_correction.reset();
    state.mode = game_mode::walking; state.give_up_prompt.reset();
    state.flight_path_points.clear(); state.picker.reset(); state.handling.reset();
    state.results_dismissed = false; state.shots_hit = 0;
    leave_cart_at_once(state);
    state.player = player_state{};
    state.player.position = anchor_on_terrain(state.area, hub.arrival);
    state.player.yaw = yaw_towards(state.player.position, hub.anchors.book.position);
    state.look = look_state{};
    state.smoking.reset(); state.drinking.reset();
    state.walk_meters_pending = state.cart_meters_pending = 0;
    park_cart(state); strap_bag_on_cart(state);
    mark_terrain_render_dirty(state);
    return true;
}

bool tee_up_hub(game_state& state, const std::size_t mat) {
    if (!state.hub || mat >= state.hub->tee_mats.size() || state.shot) { return false; }
    if (is_online(state)) {
        request_ball_preparation(state, net_command_type::tee_up, mat, true);
        return false;
    }
    const auto& spot = state.hub->tee_mats[mat];
    state.ball = my_ball{std::nullopt, resting_ball_position(state.area, spot.position,
        state.tuning.scale.ball_physics_radius_meters), 0, ball_place::teed, ball_kind::practice, mat};
    state.shot.reset(); state.online_hole = online_hole_state{};
    state.ball_correction.reset(); state.give_up_prompt.reset(); state.flight_path_points.clear();
    state.mode = game_mode::walking; state.address = address_state{};
    state.player.position = tee_stance_position(state.area, state.ball->position, spot.aim,
        state.tuning.player.ball_stand_off_distance);
    state.player.yaw = yaw_towards(state.ball->position, spot.aim);
    state.look.pitch = 0;
    return true;
}

void finish_practice_shot(game_state& state) {
    if (!state.hub || !state.ball) { return; }
    state.ball->place = ball_place::in_cup;
    state.mode = game_mode::walking; state.shot.reset(); state.flight_path_points.clear();
}
