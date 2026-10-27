#include "game/address_play.h"

#include "game/golf_bag.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/shot_ability.h"
#include "game/motion_sync.h"
#include "game/rangefinder.h"
#include "game/swing.h"
#include "game/text_ids.h"
#include "game/world_sounds.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace {
const club_definition* club_in_hand(const game_state& state) {
    return state.selected_club < state.clubs.size() ? &state.clubs[state.selected_club] : nullptr;
}

// One dial key: how long it has been held now, and the steps that made.
int held_steps(float& held, const bool down, const float dt, const setup_tuning& tuning) {
    const float before = held;
    held = !down ? -1.0f : before < 0.0f ? 0.0f : before + dt;
    return dial_steps(before, held, tuning);
}

setup_steps dial_steps_this_frame(game_state& state, const game_input& input, const float dt) {
    const setup_tuning& tuning = state.tuning.setup;
    dial_holds& holds = state.address.holds;
    setup_steps steps;
    steps.aim = held_steps(holds.aim_left, input.strafe_left_held || input.turn_left_held, dt, tuning) -
        held_steps(holds.aim_right, input.strafe_right_held || input.turn_right_held, dt, tuning) + input.aim_steps;
    steps.face = held_steps(holds.face_open, input.face_open_held, dt, tuning) -
        held_steps(holds.face_close, input.face_close_held, dt, tuning) + input.face_steps;
    steps.ball = held_steps(holds.ball_forward, input.ball_forward_held, dt, tuning) -
        held_steps(holds.ball_back, input.ball_back_held, dt, tuning) + input.ball_steps;
    steps.backswing = held_steps(holds.longer, input.forward_held, dt, tuning) -
        held_steps(holds.shorter, input.back_held, dt, tuning) + input.backswing_steps;
    steps.aim_radians = input.stance_drag_yaw;
    // The plain wheel turns whatever the view's centre is on.
    switch (my_pointed_part(state)) {
    case setup_part::face:
        steps.face += input.look_target_wheel_steps;
        break;
    case setup_part::ball:
        steps.ball += input.look_target_wheel_steps;
        break;
    case setup_part::stance:
        steps.aim += input.look_target_wheel_steps;
        break;
    case setup_part::none:
        steps.backswing += input.look_target_wheel_steps;
        break;
    }
    return steps;
}

// The mouse and the up and down keys look freely within the clamp; V turns
// the head to the other framed view, during which they wait.
void look_at_address(game_state& state, const game_input& input, const float dt) {
    address_state& address = state.address;
    const camera_tuning& camera = state.tuning.camera;
    if (address.snap) {
        address.snap->elapsed += dt;
        address.look = snap_look_at(*address.snap, camera);
        if (snap_done(*address.snap, camera)) {
            address.snap.reset();
        }
        return;
    }
    if (input.snap_view) {
        address.snap = look_snap{address.look, snap_destination(address.look, camera), 0.0f};
        return;
    }
    address_look look = address.look;
    // The mouse turns left in the world; towards the target is my own hand's way.
    look.yaw += hand_sign(address.setup.hand) * input.look_yaw;
    look.pitch += input.look_pitch + state.tuning.player.look_pitch_rate * look_pitch_direction(input) * dt;
    address.look = clamped_look(look, camera);
}

// Standing where the setup puts me, facing along it.
void stand_to_setup(game_state& state, frame_profile* profile) {
    const shot_setup& setup = state.address.setup;
    state.player.position = address_stance_position(state.area, setup, state.tuning, profile);
    state.player.yaw = setup.aim_angle;
    state.player.turn_rate = 0.0f;
}

// Offline what moves that my shot may meet: me, stood at the ball, and my
// cart where it stands (not sunk).
std::vector<snapshot_entry> offline_snapshot(const game_state& state) {
    std::vector<snapshot_entry> entries{snapshot_entry{snapshot_kind::person, state.player.position, 0.0f, true}};
    if (state.cart.motion.condition != cart_condition::sunk) {
        entries.push_back(snapshot_entry{snapshot_kind::cart, state.cart.pose.position, state.cart.pose.yaw, false});
    }
    return entries;
}

// The ball struck at impact: offline as set up with the round's next dice;
// online as the server played it (its answer).
void launch_shot(game_state& state) {
    const club_definition* club = club_in_hand(state);
    if (club == nullptr) {
        return;
    }
    my_ball& ball = *state.ball;
    const shot_setup& setup = state.address.setup;
    shot_result result;
    if (state.address.answer) {
        result = room_shot_result(state, *state.address.answer);
    } else {
        shot_course course = current_shot_course(state);
        course.weather = course_weather_at(state, setup.shot_at);
        const shot_record record{setup, next_shot_seed(state), offline_snapshot(state)};
        ++state.shots_hit;
        result = simulate_struck_shot(record, my_golfer(state), course, state.tuning, state.clubs);
    }
    play_shot(state, result);
    // Where the server says it rests wins, should this client's sum differ.
    if (state.address.answer) {
        const glm::vec3 off_by = state.address.answer->rest - result.rest_position;
        if (glm::length(off_by) > state.tuning.net.shot_correction_distance) {
            state.shot->correction = off_by;
        }
    }
    state.address.swing_elapsed.reset();
    state.address.answer.reset();
    ++ball.strokes;
    ball.place = ball_place::at_rest;
    push_club_hit(state, *club, result, setup.ball_start);
}
}

void step_up_to_ball(game_state& state) {
    const club_definition* club = club_in_hand(state);
    if (club == nullptr || !state.ball) {
        return;
    }
    if (bag_on_back(state)) {
        state.notice = game_notice{text_hud_bag_on_back, 0.0f};
        return;
    }
    const glm::vec3 ball = state.ball->position;
    address_state address;
    address.setup = stock_setup(*club, lie_at(state.area, ball), ball, state.player.yaw, state.hand,
                                natural_face_bias(*club, my_golfer(state).natural, state.tuning.golfer), state.tuning.setup);
    address.look = target_look(state.tuning.camera);
    state.address = address;
    state.mode = game_mode::addressing;
    stand_to_setup(state, nullptr);
    refresh_shot_preview(state);
}

void update_addressing(game_state& state, const game_input& input, const float dt, frame_profile* profile) {
    address_state& address = state.address;
    const club_definition* club = club_in_hand(state);
    if (club == nullptr) {
        state.mode = game_mode::walking;
        return;
    }
    if (address.swing_elapsed) {
        const swing_tempo tempo = swing_tempo_for(*club, state.tuning.swing);
        *address.swing_elapsed += dt;
        // Online the club waits at the top for the server's answer.
        if (is_online(state) && !address.answer) {
            *address.swing_elapsed = std::min(*address.swing_elapsed, tempo.back_seconds);
        }
        if (*address.swing_elapsed >= swing_impact_seconds(tempo)) {
            launch_shot(state);
        }
        return;
    }
    if (input.interact) {
        state.mode = game_mode::walking;
        return;
    }
    if (input.toggle_flag) {
        toggle_flag_in_reach(state);
    }

    look_at_address(state, input, dt);
    const setup_limits limits = setup_limits_for(*club, state.tuning.setup);
    if (input.reset_setup) {
        address.setup = stock_setup(*club, lie_at(state.area, address.setup.ball_start), address.setup.ball_start,
                                    address.setup.aim_angle, address.setup.hand,
                                    natural_face_bias(*club, my_golfer(state).natural, state.tuning.golfer), state.tuning.setup);
    }
    address.setup = stepped_setup(address.setup, dial_steps_this_frame(state, input, dt), limits, state.tuning.setup);
    if (input.aim_at_view) {
        const eye_view eye = my_address_eye(state);
        if (const std::optional<glm::vec3> ground =
                ground_hit(state.area, eye.position, eye.direction, state.tuning.rangefinder, state.tuning.scale)) {
            address.setup = clamped_setup(aimed_at(address.setup, *ground), limits);
        }
    }
    stand_to_setup(state, profile);
    refresh_shot_preview(state);

    if (input.swing) {
        address.setup.ball_start = state.ball->position;
        address.setup.shot_at = shot_unix_ms(state);
        address.swing_elapsed = 0.0f;
        address.answer.reset();
        push_audio_event(state, audio_event_type::swing_start, state.ball->position);
        // Online the server hears where I stand (others see my club go back
        // from then, and it checks I am beside the ball), then my setup.
        send_motion_now(state);
        record_shot(state, address.setup);
    }
}

shot_preview preview_shot(const game_state& state, const shot_setup& setup) {
    shot_preview preview;
    preview.setup = setup;
    const club_definition* club = find_club(state.clubs, setup.club_id);
    if (club == nullptr || !state.ball) {
        return preview;
    }
    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, shot_unix_ms(state));
    course.weather.wind = glm::vec3(0.0f);
    if (club->club_class == "putter") {
        const ball_on_lie on =
            ball_on_lie_at(state.area, setup.ball_start, setup.aim_angle, setup.hand, course.weather.wetness, state.tuning);
        const glm::vec3 along = yaw_direction(club_strike(*club, setup, on, state.tuning).launch.direction);
        const shot_preview_tuning& tuning = state.tuning.shot_preview;
        const float unit = state.tuning.scale.meters_per_world_unit;
        const int steps = std::max(1, static_cast<int>(std::lround(tuning.putt_line_meters / tuning.putt_line_step_meters)));
        for (int i = 0; i <= steps; ++i) {
            glm::vec3 point =
                setup.ball_start + along * (tuning.putt_line_meters / unit * static_cast<float>(i) / static_cast<float>(steps));
            point.y = terrain_height(state.area, point);
            preview.line.push_back(point);
        }
        return preview;
    }
    const shot_result result = simulate_shot(setup, course, state.tuning, state.clubs);
    const auto landed = std::find_if(result.events.begin(), result.events.end(),
                                     [](const shot_event& event) { return event.kind == shot_event_kind::land; });
    const float flight_end = landed != result.events.end() ? landed->time : result.duration;
    const float interval = state.tuning.shot_preview.step_seconds;
    for (float time = 0.0f; time < flight_end; time += interval) {
        preview.line.push_back(shot_position_at(result, time));
    }
    preview.line.push_back(shot_position_at(result, flight_end));
    if (landed != result.events.end()) {
        preview.landing = preview.line.back();
    }
    preview.rest = result.rest_position;
    return preview;
}

void refresh_shot_preview(game_state& state) {
    const shot_setup& setup = state.address.setup;
    const std::optional<shot_preview>& shown = state.address.preview;
    const bool same = shown && shown->setup.club_id == setup.club_id && shown->setup.aim_angle == setup.aim_angle &&
        shown->setup.face_open == setup.face_open && shown->setup.ball_position == setup.ball_position &&
        shown->setup.backswing == setup.backswing && shown->setup.ball_start == setup.ball_start &&
        shown->setup.hand == setup.hand;
    if (!same) {
        state.address.preview = preview_shot(state, setup);
    }
}

eye_view my_address_eye(const game_state& state) {
    return address_eye(state.area, state.address.setup, state.address.look, state.tuning);
}

address_marks my_address_marks(const game_state& state) {
    return address_marks_for(state.area, state.address.setup, *club_in_hand(state), state.tuning);
}

setup_part my_pointed_part(const game_state& state) {
    if (club_in_hand(state) == nullptr) {
        return setup_part::none;
    }
    const eye_view eye = my_address_eye(state);
    return pointed_part(eye.position, eye.direction, my_address_marks(state), state.tuning.setup.pointer_radius_degrees);
}
