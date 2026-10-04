#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/remote_players.h"
#include "game/round_state.h"
#include "game/scorecard.h"
#include "game/text_ids.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {
constexpr std::uint64_t me = 1;
constexpr std::uint64_t other = 2;
const std::int64_t start_time = server_micros(1000.0);

// Online on the fixture hub course, connected, with my own room rows.
game_state online_hub() {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.status = net_status::connected;
    state.online.account_id = me;
    state.online.server_now = start_time;
    room_player self;
    self.name = "ME";
    self.round_strokes = std::vector<int>(state.course_holes.size(), 0);
    self.motion = current_motion(state);
    self.motion_at = start_time;
    state.online.players[me] = self;
    return state;
}

// Playing hole 1, here and on the server, which started it 5 s ago.
void on_hole(game_state& state) {
    start_hub_hole(state, 0);
    room_player& self = state.online.players[me];
    self.zone = 0;
    self.hole_started_at = start_time - server_micros(5.0);
    self.motion = current_motion(state);
    update_game(state, game_input{}, 0.0f);
    state.net_commands.clear();
}

void play_out_shot(game_state& state) {
    for (int i = 0; i < 4000 && shot_playing(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
}

// Frames are clamped short, so a long wait is many frames.
void wait(game_state& state, const float seconds) {
    for (float waited = 0.0f; waited < seconds; waited += 0.02f) {
        update_game(state, game_input{}, 0.02f);
    }
}

const net_command* find_command(const game_state& state, const net_command_type type) {
    for (const net_command& command : state.net_commands) {
        if (command.type == type) {
            return &command;
        }
    }
    return nullptr;
}

bool has_command(const game_state& state, const net_command_type type) {
    return find_command(state, type) != nullptr;
}

void refuse(game_state& state, const std::string& reducer, const std::string& error) {
    state.online.refusals.push_back(reducer_failure{reducer, error});
    update_game(state, game_input{}, 0.0f);
}

// Another player standing `offset` from me, in my zone.
void add_other(game_state& state, const glm::vec3& offset, const std::uint64_t group_id) {
    room_player player;
    player.name = "OTHER";
    player.group_id = group_id;
    player.zone = local_zone(state);
    net_motion motion;
    motion.zone = player.zone;
    motion.position = state.player.position + offset;
    player.motion = motion;
    player.motion_at = state.online.server_now;
    state.online.players[other] = player;
    update_remote_players(state, 0.0f);
}
}

TEST_CASE("online wind time runs on the server's clock from when it started the hole") {
    game_state state = online_hub();
    on_hole(state);
    state.hole_time = 0.5f;
    CHECK(near(shot_wind_time(state), 5.0f, 0.001f));

    state.play = play_mode::offline;
    CHECK(near(shot_wind_time(state), 0.5f));

    // Before the server has me on the hole, the hole's own clock.
    state.play = play_mode::online;
    state.online.players[me].zone = hub_zone;
    CHECK(near(shot_wind_time(state), 0.5f));
}

TEST_CASE("online a cigarette is lit only once the server accepted the smoke") {
    game_state state = online_hub();
    game_input smoke;
    smoke.smoke = true;
    update_game(state, smoke, 0.016f);
    CHECK(state.smoke_emote.active);
    CHECK(!cigarette_lit(state));

    state.online.smoke_accepted_at = state.online.server_now - server_micros(1.0);
    CHECK(cigarette_lit(state));
    state.online.server_now += server_micros(state.rewards.cigarette.duration_seconds);
    CHECK(!cigarette_lit(state));

    state.play = play_mode::offline;
    CHECK(cigarette_lit(state) == (state.cigarette_seconds_left > 0.0f));
}

TEST_CASE("an online shot tells the server where the player stands first, with the server's wind time") {
    game_state state = online_hub();
    on_hole(state);
    hit_selected_club(state);
    std::size_t motion_at = state.net_commands.size();
    std::size_t shot_at = state.net_commands.size();
    for (std::size_t i = 0; i < state.net_commands.size(); ++i) {
        const net_command& command = state.net_commands[i];
        if (command.type == net_command_type::take_shot) {
            shot_at = i;
            CHECK(near(command.shot.wind_time, 5.0f, 0.001f));
        } else if (command.type == net_command_type::motion && command.motion.mode == motion_mode::aim) {
            motion_at = std::min(motion_at, i);
        }
    }
    REQUIRE(shot_at < state.net_commands.size());
    CHECK(motion_at < shot_at);
}

TEST_CASE("my shot ends where the server says, during playback or blending after it") {
    game_state state = online_hub();
    on_hole(state);
    hit_selected_club(state);
    REQUIRE(shot_playing(state));
    const glm::vec3 server_rest = state.shot->result.rest_position + glm::vec3(1.0f, 0.0f, 0.0f);
    state.online.shots.push_back(room_shot{me, 0, state.stroke_count, shot_input{}, server_rest, false});
    play_out_shot(state);
    CHECK(near(state.ball.position, server_rest, 0.001f));

    // A late answer, after the shot played here: the ball slides over.
    hit_selected_club(state);
    play_out_shot(state);
    const glm::vec3 played_to = state.ball.position;
    const glm::vec3 late_rest = played_to + glm::vec3(0.0f, 0.0f, 2.0f);
    state.online.shots.push_back(room_shot{me, 0, state.stroke_count, shot_input{}, late_rest, false});
    update_game(state, game_input{}, 0.1f);
    CHECK(glm::length(state.ball.position - played_to) > 0.1f);
    CHECK(glm::length(state.ball.position - late_rest) > 0.1f);
    wait(state, state.tuning.net.shot_correction_seconds);
    CHECK(near(state.ball.position, late_rest, 0.001f));
    CHECK(!state.ball_correction);

    // Within the correction distance nothing moves.
    const glm::vec3 rest = state.ball.position;
    state.online.shots.push_back(room_shot{me, 0, state.stroke_count, shot_input{}, rest + glm::vec3(0.01f, 0.0f, 0.0f), false});
    update_game(state, game_input{}, 0.1f);
    CHECK(near(state.ball.position, rest));
}

TEST_CASE("online the hole ends when the server says: scored when it holed my ball") {
    game_state state = online_hub();
    on_hole(state);
    // Holed here: the hole stays open until the server answers.
    const glm::vec3 cup = pin_anchor_position(state);
    state.stroke_count = 2;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball.position, cup}, {}});
    play_out_shot(state);
    CHECK(state.hole.has_value());
    CHECK(!hole_played(state.round, 0));

    state.online.shots.push_back(room_shot{me, 0, 2, shot_input{}, cup, true});
    room_player& self = state.online.players[me];
    self.zone = hub_zone;
    self.motion->position = state.hub->world.hole_starts[0].return_position;
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.hole.has_value());
    REQUIRE(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
}

TEST_CASE("online a hole the server ends without holing my ball is given up") {
    game_state state = online_hub();
    on_hole(state);
    room_player& self = state.online.players[me];
    self.zone = hub_zone;
    self.motion->position = state.hub->world.hole_starts[0].position;
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.hole.has_value());
    CHECK(!hole_played(state.round, 0));
    CHECK(near(state.player.position, state.hub->world.hole_starts[0].position, 0.001f));
}

TEST_CASE("a refused shot goes back to the server's ball and says why") {
    game_state state = online_hub();
    on_hole(state);
    const glm::vec3 tee = state.ball.position;
    state.online.balls[me] = room_ball{0, tee, 0};
    hit_selected_club(state);
    REQUIRE(shot_playing(state));
    refuse(state, "take_shot", "too_far");
    CHECK(!shot_playing(state));
    CHECK(state.stroke_count == 0);
    CHECK(near(state.ball.position, tee));
    CHECK(state.mode == game_mode::walking);
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == "online.error.too_far");

    wait(state, state.tuning.net.notice_seconds);
    CHECK(!state.notice.has_value());
}

TEST_CASE("a refused enter_hole or claim puts the player back where the server has them") {
    game_state state = online_hub();
    const glm::vec3 accepted = state.player.position;
    start_hub_hole(state, 0);
    REQUIRE(state.hole.has_value());
    refuse(state, "enter_hole", "too_far");
    CHECK(!state.hole.has_value());
    CHECK(near(state.player.position, accepted, 0.001f));

    state.player.position += glm::vec3(5.0f, 0.0f, 0.0f);
    refuse(state, "claim_collectible", "too_far");
    CHECK(near(state.player.position, accepted, 0.001f));
}

TEST_CASE("moving too fast snaps back to the last accepted position, only within the same zone") {
    game_state state = online_hub();
    const glm::vec3 accepted = state.player.position;
    state.player.position += glm::vec3(30.0f, 0.0f, 0.0f);
    refuse(state, "update_motion", "too_fast");
    CHECK(near(state.player.position, accepted, 0.001f));

    // Entering a hole the server has not answered yet is not undone.
    start_hub_hole(state, 0);
    refuse(state, "update_motion", "too_fast");
    CHECK(state.hole.has_value());
}

TEST_CASE("a hole the server holds me on but I left is given up there") {
    game_state state = online_hub();
    state.online.players[me].zone = 0;
    refuse(state, "update_motion", "wrong_zone");
    CHECK(has_command(state, net_command_type::return_to_hub));
    CHECK(!state.hole.has_value());
}

TEST_CASE("G joins the nearest player's group when it has space, else starts one; Shift+G leaves") {
    game_state state = online_hub();
    const float reach = state.tuning.net.group_join_distance;
    add_other(state, glm::vec3(reach * 0.5f, 0.0f, 0.0f), 5);
    state.online.group_sizes[5] = state.tuning.server.group_capacity - 1;

    game_input g;
    g.group = true;
    update_game(state, g, 0.0f);
    REQUIRE(has_command(state, net_command_type::join_group));
    CHECK(find_command(state, net_command_type::join_group)->group_id == 5U);

    state.net_commands.clear();
    state.online.group_sizes[5] = state.tuning.server.group_capacity;
    update_game(state, g, 0.0f);
    CHECK(has_command(state, net_command_type::create_group));
    CHECK(!has_command(state, net_command_type::join_group));

    state.net_commands.clear();
    state.online.group_sizes[5] = 1;
    add_other(state, glm::vec3(reach * 2.0f, 0.0f, 0.0f), 5);
    update_game(state, g, 0.0f);
    CHECK(has_command(state, net_command_type::create_group));

    state.net_commands.clear();
    game_input leave;
    leave.leave_group = true;
    update_game(state, leave, 0.0f);
    CHECK(has_command(state, net_command_type::leave_group));

    state.play = play_mode::offline;
    state.net_commands.clear();
    update_game(state, g, 0.0f);
    update_game(state, leave, 0.0f);
    CHECK(state.net_commands.empty());
}

TEST_CASE("others are carried on along their motion, capped, on the ground, with corrections blended in") {
    game_state state = online_hub();
    add_other(state, glm::vec3(0.0f), 0);
    room_player& player = state.online.players[other];
    player.motion->mode = motion_mode::walk;
    player.motion->yaw = 0.0f;
    player.motion->speed = 1.0f;
    const glm::vec3 from = player.motion->position;

    state.online.server_now = player.motion_at + server_micros(1.0);
    update_remote_players(state, 0.0f);
    const glm::vec3 one_second = state.remote_avatars.at(other).position;
    CHECK(near(horizontal_distance(one_second, from), 1.0f, 0.01f));
    CHECK(near(one_second.y, terrain_height(state.area, one_second), 0.001f));

    state.online.server_now = player.motion_at + server_micros(10.0);
    update_remote_players(state, 0.0f);
    CHECK(near(horizontal_distance(state.remote_avatars.at(other).position, from), state.tuning.net.remote_extrapolation_seconds, 0.01f));

    // A new motion: the avatar slides there instead of jumping.
    const glm::vec3 shown = state.remote_avatars.at(other).position;
    player.motion->position = from + glm::vec3(4.0f, 0.0f, 0.0f);
    player.motion->speed = 0.0f;
    player.motion_at = state.online.server_now;
    update_remote_players(state, 0.0f);
    CHECK(near(state.remote_avatars.at(other).position, shown, 0.001f));
    update_remote_players(state, state.tuning.net.remote_correction_seconds);
    CHECK(near(horizontal_distance(state.remote_avatars.at(other).position, player.motion->position), 0.0f, 0.001f));
}

TEST_CASE("while reconnecting others stand still, and players who left disappear") {
    game_state state = online_hub();
    add_other(state, glm::vec3(0.0f), 0);
    state.online.players[other].motion->speed = 1.0f;
    const glm::vec3 shown = state.remote_avatars.at(other).position;
    state.online.status = net_status::signing_in;
    state.online.server_now += server_micros(2.0);
    update_remote_players(state, 0.5f);
    CHECK(near(state.remote_avatars.at(other).position, shown));

    state.online.status = net_status::connected;
    state.online.players.erase(other);
    update_remote_players(state, 0.0f);
    CHECK(state.remote_avatars.empty());
}

TEST_CASE("another player's shot plays from the server's event and ends where it says") {
    game_state state = online_hub();
    const hub_hole_marker& marker = state.hub->markers[0];
    shot_input input;
    input.ball_start = resting_ball_position(state.area, marker.tee_position, state.ball.radius);
    input.aim_angle = yaw_towards(marker.tee_position, marker.pin_position);
    input.club_id = state.clubs.front().id;
    input.power = 0.8f;
    const glm::vec3 rest = input.ball_start + glm::vec3(3.0f, 0.0f, 3.0f);
    state.online.balls[other] = room_ball{0, rest, 1};
    state.online.shots.push_back(room_shot{other, 0, 1, input, rest, false});
    update_game(state, game_input{}, 0.0f);
    REQUIRE(state.remote_shots.count(other) == 1U);
    CHECK(state.online.shots.empty());

    // While it plays its ball is where the shot is, not where it will rest.
    update_game(state, game_input{}, 0.2f);
    std::vector<shown_ball> balls = remote_balls(state);
    REQUIRE(balls.size() == 1U);
    CHECK(!near(balls[0].position, rest, 0.01f));

    for (int i = 0; i < 4000 && remote_shot_playing(state.remote_shots.at(other)); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    balls = remote_balls(state);
    REQUIRE(balls.size() == 1U);
    CHECK(near(balls[0].position, rest, 0.001f));
    CHECK(state.remote_shots.at(other).trail.size() > 2U);

    // The trail fades out, then goes.
    wait(state, state.tuning.net.remote_trail_fade_seconds + 0.05f);
    CHECK(state.remote_shots.empty());
}

TEST_CASE("the group's scorecard has a row per member from the server") {
    game_state state = online_hub();
    state.online.players[me].group_id = 3;
    state.online.players[me].round_strokes[0] = state.course_holes[0].par + 1;
    add_other(state, glm::vec3(0.0f), 3);
    state.online.players[other].round_strokes = std::vector<int>(state.course_holes.size(), 0);
    room_player stranger = state.online.players[other];
    stranger.group_id = 0;
    state.online.players[9] = stranger;

    const std::vector<group_scorecard_row> rows = build_group_scorecard(state, shipped_text_assets().strings);
    REQUIRE(rows.size() == 2U);
    CHECK(rows[0].me);
    CHECK(rows[0].name == "ME");
    CHECK(rows[0].holes_played == 1);
    CHECK(rows[0].relative_label == "+1");
    CHECK(rows[1].name == "OTHER");
    CHECK(rows[1].holes_played == 0);

    state.online.players[me].group_id = 0;
    CHECK(build_group_scorecard(state, shipped_text_assets().strings).empty());
}

TEST_CASE("a finished round goes on to the next at the last hole's return point") {
    game_state state = online_hub();
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        start_hub_hole(state, i);
        state.stroke_count = 3;
        complete_current_hole(state);
    }
    REQUIRE(round_finished(state.round));
    const std::size_t last = state.course_holes.size() - 1;
    start_next_round(state);
    CHECK(!round_finished(state.round));
    CHECK(!state.hole.has_value());
    CHECK(near(horizontal_distance(state.player.position, state.hub->world.hole_starts[last].return_position), 0.0f, 0.001f));
}
