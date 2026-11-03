#include "game/address_play.h"

#include "game/address_view.h"
#include "game/golf_bag.h"
#include "game/golf_cart.h"
#include "game/mode_dispatch.h"
#include "game/remote_players.h"
#include "game/shot_ability.h"
#include "game/shot_number.h"
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

// Whole steps out of a drag's `counts` so far and `moved` more, keeping
// the rest for the next.
int pulled_steps(float& counts, const float moved, const int counts_per_step) {
    const float per_step = static_cast<float>(std::max(1, counts_per_step));
    counts += moved;
    const int steps = static_cast<int>(counts / per_step);
    counts -= static_cast<float>(steps) * per_step;
    return steps;
}

// A and D turn the stance, and W, S and the wheel set the backswing, from
// either look. Looking down at the ball, the arrows and a left-drag pull
// the face (up opens it) and the ball, which moves the way it goes on the
// screen: to my lead side is forward in the stance. Looking up, a
// right-drag turns the stance finely.
setup_steps dial_steps_this_frame(game_state& state, const game_input& input, const float dt, const bool down) {
    const setup_tuning& tuning = state.tuning.setup;
    dial_holds& holds = state.address.holds;
    setup_steps steps;
    steps.aim = held_steps(holds.aim_left, input.strafe_left_held, dt, tuning) -
        held_steps(holds.aim_right, input.strafe_right_held, dt, tuning);
    steps.backswing = held_steps(holds.longer, input.forward_held, dt, tuning) -
        held_steps(holds.shorter, input.back_held, dt, tuning) + input.backswing_steps;
    if (!down) {
        holds.pull_x = 0.0f;
        holds.pull_y = 0.0f;
        steps.aim_radians = input.stance_drag_yaw;
    }
    steps.face = held_steps(holds.face_open, down && input.look_up_held, dt, tuning) -
        held_steps(holds.face_close, down && input.look_down_held, dt, tuning) -
        (down ? pulled_steps(holds.pull_y, input.pull_y, tuning.pull_counts_per_step) : 0);
    const int screen_left = held_steps(holds.ball_left, down && input.turn_left_held, dt, tuning) -
        held_steps(holds.ball_right, down && input.turn_right_held, dt, tuning) -
        (down ? pulled_steps(holds.pull_x, input.pull_x, tuning.pull_counts_per_step) : 0);
    steps.ball = screen_left * static_cast<int>(hand_sign(state.address.setup.hand));
    return steps;
}

// The mouse looks freely within the clamp; R turns the head between looking
// down and up (again mid-turn, back the other way), during which it waits.
// F only ever turns it down to the ball.
void look_at_address(game_state& state, const game_input& input, const bool turning) {
    address_state& address = state.address;
    const camera_tuning& camera = state.tuning.camera;
    const address_look heading = address.snap ? address.snap->to : address.look;
    if (input.interact && !input.snap_view) {
        if (!looking_down(heading, camera)) {
            address.snap = look_snap{address.look, down_look(camera), 0.0f};
        }
        return;
    }
    if (input.snap_view) {
        address.snap = look_snap{address.look, snap_destination(address.snap ? address.snap->to : address.look, camera), 0.0f};
        return;
    }
    if (turning) {
        return;
    }
    address_look look = address.look;
    // The mouse turns left in the world; towards the target is my own hand's way.
    look.yaw += hand_sign(address.setup.hand) * input.look_yaw;
    look.pitch += input.look_pitch;
    address.look = clamped_look(look, camera);
}

// An R turn on by `dt`; whether one was under way.
bool turn_head(address_state& address, const float dt, const camera_tuning& camera) {
    if (!address.snap) {
        return false;
    }
    address.snap->elapsed += dt;
    address.look = snap_look_at(*address.snap, camera);
    if (snap_done(*address.snap, camera)) {
        address.snap.reset();
    }
    return true;
}

// A swipe's progress after `moved` more: +1 a whole swipe right, -1 left,
// 0 not yet. Turning back starts a new one.
int swiped(float& progress, const float moved) {
    if (moved * progress < 0.0f) {
        progress = 0.0f;
    }
    progress += moved;
    if (std::abs(progress) < 1.0f) {
        return 0;
    }
    const int way = progress > 0.0f ? 1 : -1;
    progress = 0.0f;
    return way;
}

// Standing where the setup puts me, facing along it.
void stand_to_setup(game_state& state, frame_profile* profile) {
    const shot_setup& setup = state.address.setup;
    state.player.position = stance_at_address(state.area, setup, state.tuning, profile).position;
    // Heading along the line, the way I walk off; my figure faces the ball.
    state.player.yaw = setup.aim_angle;
    state.player.turn_rate = 0.0f;
}

// Offline what moves that my shot may meet: me, stood at the ball, and my
// cart where it stands (not sunk).
std::vector<snapshot_entry> offline_snapshot(const game_state& state) {
    std::vector<snapshot_entry> entries{snapshot_entry{snapshot_kind::person, state.player.position, 0.0f, true}};
    if (own_cart(state) && state.cart.motion.condition != cart_condition::sunk) {
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
    std::optional<shot_number> number;
    const ball_lie lie = lie_at(state.area, setup.ball_start);
    if (state.address.answer) {
        result = room_shot_result(state, *state.address.answer);
        number = shot_number_for(state.address.answer->record, state.address.answer->golfer, lie,
                                 state.course.green_stimp_feet, state.tuning, state.clubs);
    } else {
        shot_course course = current_shot_course(state);
        course.weather = course_weather_at(state, setup.shot_at);
        const shot_record record{setup, next_shot_seed(state), offline_snapshot(state)};
        ++state.shots_hit;
        result = simulate_struck_shot(record, my_golfer(state), course, state.tuning, state.clubs);
        number = shot_number_for(record, my_golfer(state), lie, state.course.green_stimp_feet, state.tuning, state.clubs);
    }
    if (number) {
        state.shot_number = shot_number_popup{*number, 0.0f};
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

// The setup committed and the downswing under way from the top.
void start_swing(game_state& state) {
    address_state& address = state.address;
    address.setup.ball_start = state.ball->position;
    address.setup.shot_at = shot_unix_ms(state);
    address.swing_elapsed = swing_tempo_for(*club_in_hand(state), state.tuning.swing).back_seconds;
    address.answer.reset();
    push_audio_event(state, audio_event_type::swing_start, state.ball->position);
    // Online the server hears where I stand (others see my club go back
    // from then, and it checks I am beside the ball), then my setup.
    send_motion_now(state);
    record_shot(state, address.setup);
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

void set_up_as(game_state& state, const address_state& last) {
    const club_definition* club = club_in_hand(state);
    address_state& address = state.address;
    if (club == nullptr || state.mode != game_mode::addressing || last.setup.club_id != address.setup.club_id) {
        return;
    }
    shot_setup setup = address.setup;
    setup.aim_angle = last.setup.aim_angle;
    setup.face_open = last.setup.face_open;
    setup.ball_position = last.setup.ball_position;
    setup.backswing = last.setup.backswing;
    address.setup = clamped_setup(setup, setup_limits_for(*club, state.tuning.setup));
    address.look = last.look;
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
    const swing_tempo tempo = swing_tempo_for(*club, state.tuning.swing);
    if (address.swing_elapsed) {
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
    const setup_limits limits = setup_limits_for(*club, state.tuning.setup);
    if (address.meter_elapsed) {
        // The club going back: Esc stops it, a swipe left (or Space) starts
        // down from as far as it got.
        *address.meter_elapsed += dt;
        if (input.cancel) {
            address.meter_elapsed.reset();
            address.swipe = 0.0f;
        } else if (swiped(address.swipe, input.swipe) < 0 || input.swing) {
            const float elapsed = *address.meter_elapsed;
            const float cap = address.setup.backswing;
            address.setup.backswing = swing_meter_at(elapsed, cap, state.tuning.swing);
            address.setup.strike_timing = strike_timing_at(elapsed, cap, state.tuning.swing);
            address.setup = clamped_setup(address.setup, limits);
            address.meter_elapsed.reset();
            start_swing(state);
        }
        return;
    }
    if (input.toggle_flag) {
        toggle_flag_in_reach(state);
    }

    look_at_address(state, input, turn_head(address, dt, state.tuning.camera));
    const bool down = !address.snap && looking_down(address.look, state.tuning.camera);
    if (input.reset_setup) {
        address.setup = stock_setup(*club, lie_at(state.area, address.setup.ball_start), address.setup.ball_start,
                                    address.setup.aim_angle, address.setup.hand,
                                    natural_face_bias(*club, my_golfer(state).natural, state.tuning.golfer), state.tuning.setup);
    }
    address.setup = stepped_setup(address.setup, dial_steps_this_frame(state, input, dt, down), limits, state.tuning.setup);
    if (input.aim_at_view && !down) {
        const eye_view eye = my_address_eye(state);
        if (const std::optional<glm::vec3> ground =
                ground_hit(state.area, eye.position, eye.direction, state.tuning.rangefinder, state.tuning.scale)) {
            address.setup = clamped_setup(aimed_at(address.setup, *ground), limits);
        }
    }
    stand_to_setup(state, profile);
    refresh_shot_preview(state);

    // The swing starts only looking down at the ball.
    if (!down) {
        address.swipe = 0.0f;
        return;
    }
    if (swiped(address.swipe, input.swipe) > 0 || input.swing) {
        address.meter_elapsed = 0.0f;
        address.swipe = 0.0f;
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
    // Only how it starts: how far it goes is the player's to know.
    const shot_result result = simulate_shot(setup, course, state.tuning, state.clubs);
    const float start_line = state.tuning.shot_preview.start_line_meters / state.tuning.scale.meters_per_world_unit;
    const float interval = state.tuning.shot_preview.step_seconds;
    // A short shot's line ends where it first comes down, so it fades out
    // over its whole flight to there.
    const auto landing = std::find_if(result.events.begin(), result.events.end(),
                                      [](const shot_event& event) { return event.kind == shot_event_kind::land; });
    const float flight_end = landing != result.events.end() ? landing->time : result.duration;
    for (float time = 0.0f; time < result.duration; time += interval) {
        const glm::vec3 point = shot_position_at(result, std::min(time, flight_end));
        preview.line.push_back(point);
        if (horizontal_distance(point, setup.ball_start) >= start_line || time >= flight_end) {
            break;
        }
    }
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
