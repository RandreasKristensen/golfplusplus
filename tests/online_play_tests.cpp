#include "doctest.h"

#include "game/course_session.h"
#include "game/group_round.h"
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

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace {
// `seconds` as server time (microseconds).
std::int64_t server_micros(const double seconds) {
    return static_cast<std::int64_t>(seconds * 1000000.0);
}

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
        } else if (command.type == net_command_type::motion && at_ball(command.motion.mode)) {
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
    const glm::vec3 stance = state.player.position;
    state.stroke_count = 2;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball.position, cup}, {}});
    play_out_shot(state);
    CHECK(state.hole.has_value());
    CHECK(!hole_played(state.round, 0));

    state.online.shots.push_back(room_shot{me, 0, 2, shot_input{}, cup, true});
    room_player& self = state.online.players[me];
    self.zone = hub_zone;
    // The server leaves the player where they hit from, as offline.
    self.motion->position = stance;
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.hole.has_value());
    REQUIRE(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
    CHECK(near(horizontal(state.player.position), horizontal(stance), 0.001f));
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

TEST_CASE("a tee is in use while another player's ball waits on it for the tee shot") {
    std::map<std::uint64_t, room_ball> balls;
    CHECK(!tee_in_use(balls, me, 0));
    balls[me] = room_ball{0, glm::vec3(0.0f), 0};
    CHECK(!tee_in_use(balls, me, 0));  // my own ball never blocks me
    balls[other] = room_ball{1, glm::vec3(0.0f), 0};
    CHECK(!tee_in_use(balls, me, 0));  // on another hole
    CHECK(tee_in_use(balls, me, 1));
    balls[other].stroke_count = 1;
    CHECK(!tee_in_use(balls, me, 1));  // teed off: the tee is free
}

TEST_CASE("online, a tee another player is using is not taken up and says why") {
    game_state state = online_hub();
    state.online.balls[other] = room_ball{0, glm::vec3(0.0f), 0};
    CHECK(!start_hub_hole(state, 0));
    CHECK(!state.hole.has_value());
    CHECK(state.net_commands.empty());
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_online_tee_in_use);

    state.online.balls[other].stroke_count = 1;
    CHECK(start_hub_hole(state, 0));
    CHECK(state.hole.has_value());
}

TEST_CASE("offline, the tee is never in use") {
    game_state state = started_game(fixture_hub_course());
    state.online.balls[other] = room_ball{0, glm::vec3(0.0f), 0};
    CHECK(start_hub_hole(state, 0));
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

TEST_CASE("another player's emote from the server plays on their avatar, as mine does") {
    game_state state = online_hub();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    state.online.emotes.push_back(room_emote{other, emote_id::smoke});
    state.online.emotes.push_back(room_emote{7, emote_id::drink});  // nobody shown here
    update_game(state, game_input{}, 0.0f);
    CHECK(state.online.emotes.empty());
    const remote_avatar& avatar = state.remote_avatars.at(other);
    CHECK(avatar.smoke_emote.active);
    CHECK(!avatar.drink_emote.active);

    update_game(state, game_input{}, 0.02f);
    CHECK(state.remote_avatars.at(other).smoke_emote.elapsed > 0.0f);
    state.online.emotes.push_back(room_emote{other, emote_id::drink});
    wait(state, state.tuning.player.emote_seconds * 0.5f);
    CHECK(state.remote_avatars.at(other).smoke_emote.active);
    CHECK(state.remote_avatars.at(other).drink_emote.active);
    wait(state, state.tuning.player.emote_seconds);
    CHECK(!state.remote_avatars.at(other).smoke_emote.active);
    CHECK(!state.remote_avatars.at(other).drink_emote.active);
}

TEST_CASE("another player addresses their ball and swings with their meter, their shot leaving as it arrives") {
    game_state state = online_hub();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    state.online.emotes.push_back(room_emote{other, emote_id::drink});
    update_game(state, game_input{}, 0.0f);
    REQUIRE(state.remote_avatars.at(other).drink_emote.active);
    const hub_hole_marker& marker = state.hub->markers[0];
    shot_input input;
    input.ball_start = resting_ball_position(state.area, marker.tee_position, state.ball.radius);
    input.aim_angle = yaw_towards(marker.tee_position, marker.pin_position);
    input.club_id = state.clubs.front().id;
    input.power = 0.8f;
    state.online.balls[other] = room_ball{0, input.ball_start, 0};
    room_player& player = state.online.players.at(other);
    player.motion->yaw = input.aim_angle;
    CHECK(!remote_address_pose(state, other).has_value());  // walking
    // Lining it up: no club yet, turning as they turn.
    player.motion->mode = motion_mode::aim;
    CHECK(!remote_address_pose(state, other).has_value());

    // Addressing it: the club down at the ball.
    player.motion->mode = motion_mode::address;
    std::optional<remote_address> address = remote_address_pose(state, other);
    REQUIRE(address.has_value());
    CHECK(near(address->ball_position, input.ball_start));
    CHECK(address->aim_angle == input.aim_angle);
    CHECK(address->club_power == 0.0f);

    // Swinging: the club rises with the meter from when the server heard it.
    player.motion->mode = motion_mode::swing;
    player.motion_at = state.online.server_now;
    state.online.server_now += server_micros(0.3);
    address = remote_address_pose(state, other);
    REQUIRE(address.has_value());
    CHECK(near(address->club_power, swing_meter_power(0.3f, 1.0f, state.tuning.swing.meter_cycle_seconds), 0.001f));
    CHECK(address->club_power > 0.1f);

    // The shot leaves as it arrives, and its swing is over.
    const glm::vec3 rest = input.ball_start + glm::vec3(3.0f, 0.0f, 3.0f);
    state.online.balls[other].position = rest;
    state.online.shots.push_back(room_shot{other, 0, 1, input, rest, false});
    update_game(state, game_input{}, 0.0f);
    CHECK(!remote_address_pose(state, other).has_value());
    // Hitting it doesn't end their emotes: the server and their own game don't either.
    CHECK(state.remote_avatars.at(other).drink_emote.active);
    wait(state, 0.1f);
    std::vector<shown_ball> balls = remote_balls(state);
    REQUIRE(balls.size() == 1U);
    CHECK(!near(balls[0].position, input.ball_start, 0.01f));

    // Their next address shows the club again.
    player.motion->mode = motion_mode::address;
    player.motion_at = state.online.server_now;
    CHECK(remote_address_pose(state, other).has_value());
}

TEST_CASE("my swing tells the server it started, and the shot where I stand") {
    game_state state = online_hub();
    on_hole(state);
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(current_motion(state).mode == motion_mode::address);
    CHECK(current_motion(state).turn_rate == 0.0f);

    state.net_commands.clear();
    update_game(state, action_input(), 0.0f);
    REQUIRE(state.swing.phase == swing_phase::timing);
    REQUIRE(!state.net_commands.empty());
    CHECK(state.net_commands.back().type == net_command_type::motion);
    CHECK(state.net_commands.back().motion.mode == motion_mode::swing);
}

TEST_CASE("the group's scorecard has a row per member from the server") {
    game_state state = online_hub();
    state.online.players[me].group_id = 3;
    state.round = complete_hole(state.round, 0, state.course_holes[0].par + 1);
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

TEST_CASE("a finished round goes on to the next where the player stands") {
    game_state state = online_hub();
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        start_hub_hole(state, i);
        state.stroke_count = 3;
        complete_current_hole(state);
        pick_up_from_cup(state);
    }
    REQUIRE(round_finished(state.round));
    const glm::vec3 standing = state.player.position;
    REQUIRE(horizontal_distance(standing, state.hub->markers.back().tee_position) < 5.0f);
    start_next_round(state);
    CHECK(!round_finished(state.round));
    CHECK(!state.hole.has_value());
    CHECK(near(horizontal(state.player.position), horizontal(standing), 0.001f));
}

namespace {
// Grouped with the other player, every hole holed in 3 but the last, which
// I finish while they still play it. Returns the last hole.
std::size_t finished_before_group(game_state& state) {
    state.online.players[me].group_id = 3;
    add_other(state, glm::vec3(4.0f, 0.0f, 0.0f), 3);
    const std::size_t last = state.course_holes.size() - 1U;
    state.online.players[other].round_strokes = std::vector<int>(state.course_holes.size(), 0);
    state.online.players[other].round_strokes[0] = 5;
    state.online.players[other].zone = static_cast<int>(last);
    for (std::size_t i = 0; i <= last; ++i) {
        start_hub_hole(state, i);
        state.stroke_count = 3;
        complete_current_hole(state);
        pick_up_from_cup(state);
    }
    return last;
}
}

TEST_CASE("a finished round's results wait while my group still plays the hole") {
    game_state state = online_hub();
    const std::size_t last = finished_before_group(state);
    REQUIRE(round_finished(state.round));
    CHECK(waiting_for_group(state));
    CHECK(!round_results_shown(state));
    update_game(state, game_input{}, 0.0f);  // their round so far arrives

    // The room plays on while I wait: their holing shot plays, and the
    // server has ended their round (cleared its strokes).
    const hub_hole_marker& marker = state.hub->markers[last];
    shot_input input;
    input.ball_start = resting_ball_position(state.area, marker.pin_position + glm::vec3(2.0f, 0.0f, 0.0f), state.ball.radius);
    input.aim_angle = yaw_towards(input.ball_start, marker.pin_position);
    input.club_id = state.clubs.back().id;
    input.power = 0.3f;
    state.online.shots.push_back(room_shot{other, static_cast<int>(last), 4, input, marker.pin_position, true});
    state.online.players[other].zone = hub_zone;
    state.online.players[other].round_strokes = std::vector<int>(state.course_holes.size(), 0);
    update_game(state, game_input{}, 0.0f);
    CHECK(state.online.shots.empty());
    REQUIRE(state.remote_shots.count(other) == 1U);
    CHECK(waiting_for_group(state));  // their ball is still rolling

    for (int i = 0; i < 4000 && waiting_for_group(state); ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(!waiting_for_group(state));
    CHECK(round_results_shown(state));

    // Everyone's scores, their last hole included.
    const std::vector<group_scorecard_row> rows = build_group_scorecard(state, shipped_text_assets().strings);
    REQUIRE(rows.size() == 2U);
    CHECK(rows[0].holes_played == static_cast<int>(state.course_holes.size()));
    CHECK(rows[0].strokes == 3 * static_cast<int>(state.course_holes.size()));
    CHECK(rows[1].holes_played == 2);
    CHECK(rows[1].strokes == 9);

    start_next_round(state);
    CHECK(state.group_strokes.empty());
}

TEST_CASE("waiting for the group I can walk, seen in the hub, but play nothing") {
    game_state state = online_hub();
    finished_before_group(state);
    REQUIRE(waiting_for_group(state));
    state.net_commands.clear();
    const glm::vec3 before = state.player.position;

    game_input walk;
    walk.forward_held = true;
    walk.action = true;
    for (int i = 0; i < 30; ++i) {
        update_game(state, walk, 0.05f);
    }
    CHECK(horizontal_distance(state.player.position, before) > 1.0f);
    CHECK(state.mode == game_mode::walking);
    const auto motion = std::find_if(state.net_commands.begin(), state.net_commands.end(),
                                     [](const net_command& command) { return command.type == net_command_type::motion; });
    REQUIRE(motion != state.net_commands.end());
    CHECK(motion->motion.zone == hub_zone);  // where holing out put me on the server
}

TEST_CASE("results wait for nobody who gave up the hole or left the group") {
    game_state state = online_hub();
    const std::size_t last = finished_before_group(state);
    REQUIRE(waiting_for_group(state));
    state.online.players[other].zone = hub_zone;  // gave up
    CHECK(round_results_shown(state));

    state.online.players[other].zone = static_cast<int>(last);
    REQUIRE(waiting_for_group(state));
    state.online.players[other].group_id = 0;  // left the group
    CHECK(round_results_shown(state));

    state.online.players[other].group_id = 3;
    REQUIRE(waiting_for_group(state));
    state.online.players.erase(other);  // left the room
    CHECK(round_results_shown(state));
}

TEST_CASE("solo and offline results show as the round finishes") {
    game_state state = online_hub();
    add_other(state, glm::vec3(4.0f, 0.0f, 0.0f), 0);
    state.online.players[other].zone = static_cast<int>(state.course_holes.size() - 1U);
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        start_hub_hole(state, i);
        state.stroke_count = 3;
        complete_current_hole(state);
        pick_up_from_cup(state);
    }
    CHECK(round_results_shown(state));

    game_state offline = started_game(fixture_hub_course());
    for (std::size_t i = 0; i < offline.course_holes.size(); ++i) {
        start_hub_hole(offline, i);
        offline.stroke_count = 3;
        complete_current_hole(offline);
        pick_up_from_cup(offline);
    }
    CHECK(!waiting_for_group(offline));
    CHECK(round_results_shown(offline));
}

namespace {
// Holed hole 0 and back in the hub, as the server has me; at the cup.
void holed_at_cup(game_state& state) {
    on_hole(state);
    state.stroke_count = 3;
    complete_current_hole(state);
    room_player& self = state.online.players[me];
    self.zone = hub_zone;
    REQUIRE(state.cup_ball.has_value());
    state.player.position = *state.cup_ball;
    self.motion = current_motion(state);
    state.net_commands.clear();
}

bool sent(const game_state& state, const net_command_type type) {
    return std::any_of(state.net_commands.begin(), state.net_commands.end(),
                       [type](const net_command& command) { return command.type == type; });
}
}

TEST_CASE("online the ball is picked out of its cup with the server, and goes back when refused") {
    game_state state = online_hub();
    holed_at_cup(state);
    update_game(state, action_input(), 0.0f);
    CHECK(!state.cup_ball.has_value());
    CHECK(sent(state, net_command_type::pick_up_ball));

    state.online.refusals.push_back(reducer_failure{"pick_up_ball", "too_far"});
    update_game(state, game_input{}, 0.0f);
    CHECK(state.cup_ball.has_value());
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == online_error_text_key("too_far"));
}

TEST_CASE("online a hole refused for the ball in the cup puts it back there") {
    game_state state = online_hub();
    holed_at_cup(state);
    update_game(state, action_input(), 0.0f);
    REQUIRE(!state.cup_ball.has_value());

    REQUIRE(start_hub_hole(state, 1));
    state.online.refusals.push_back(reducer_failure{"enter_hole", "ball_in_cup"});
    update_game(state, game_input{}, 0.0f);
    CHECK(in_hub(state));
    CHECK(state.cup_ball.has_value());
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_ball_in_cup);
}
