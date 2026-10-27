#include "doctest.h"

#include "game/course_session.h"
#include "game/group_round.h"
#include "game/game_state.h"
#include "game/golf_cart.h"
#include "game/interactables.h"
#include "game/golf_bag.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/player_jump.h"
#include "game/swing.h"
#include "game/remote_players.h"
#include "game/round_state.h"
#include "game/scorecard.h"
#include "game/server_errors.h"
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

// Playing hole 1, here and on the server, whose ball row has it teed.
void on_hole(game_state& state) {
    tee_up(state, 0, false);
    state.online.balls[me] = room_ball{0, state.ball->position, 0, false};
    room_player& self = state.online.players[me];
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

// Another player's cart parked `offset` from me, nobody in it.
void park_other_cart(game_state& state, const glm::vec3& offset) {
    state.online.carts[other] = room_cart{state.player.position + offset, 0.0f, 0};
}

// Stood beside the passenger seat of the other player's cart.
void beside_other_seat(game_state& state) {
    const cart_pose cart = *shown_cart(state, other);
    state.player.position =
        anchor_on_terrain(state.area, step_out_position(cart, cart_seat::passenger, state.tuning.cart));
}

void play_cart_step(game_state& state) {
    const int frames = static_cast<int>(state.tuning.cart.step_seconds / 0.02f) + 2;
    for (int i = 0; i < frames; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
}

// Another player standing `offset` from me.
void add_other(game_state& state, const glm::vec3& offset, const std::uint64_t group_id) {
    room_player player;
    player.name = "OTHER";
    player.group_id = group_id;
    net_motion motion;
    motion.position = state.player.position + offset;
    player.motion = motion;
    player.motion_at = state.online.server_now;
    state.online.players[other] = player;
    update_remote_players(state, 0.0f);
}
}

TEST_CASE("a shot's time is the server's clock online and mine offline, and the god menu's weather is offline only") {
    game_state state = online_hub();
    on_hole(state);
    state.local_unix_ms = 5000;
    CHECK(shot_unix_ms(state) == start_time / 1000);

    state.play = play_mode::offline;
    CHECK(shot_unix_ms(state) == 5000);

    weather_state held;
    held.wind = glm::vec3(3.0f, 0.0f, 0.0f);
    state.weather_override = held;
    CHECK(course_weather_at(state, 5000).wind == held.wind);
    state.play = play_mode::online;
    CHECK(course_weather_at(state, 5000).wind != held.wind);
}

TEST_CASE("online the effects on me are what I equipped and what the server runs, not my own") {
    game_state state = online_hub();
    item_effect boost;
    boost.target = effect_target_all_xp;
    boost.bucket = effect_bucket::increased;
    boost.value = 1.0f;
    boost.duration = effect_duration::seconds;
    boost.seconds = 10.0f;
    const std::int64_t now = shot_unix_ms(state);
    state.effects.push_back(active_effect{"offline_charm", boost, now, now + 10000});
    CHECK(current_effects(state).empty());
    state.online.effects.push_back(active_effect{"lucky_charm", boost, now, now + 10000});
    REQUIRE(current_effects(state).size() == 1U);
    CHECK(current_effects(state).front().item == "lucky_charm");

    // Using an item asks the server; nothing is earned or started here.
    game_input smoke;
    smoke.use_smokable = true;
    update_game(state, smoke, 0.016f);
    CHECK(state.smoking.has_value());
    CHECK(state.online.effects.size() == 1U);
}

TEST_CASE("an online shot tells the server where the player stands first, at the server's time") {
    game_state state = online_hub();
    on_hole(state);
    hit_selected_club(state);
    std::size_t motion_at = state.net_commands.size();
    std::size_t shot_at = state.net_commands.size();
    for (std::size_t i = 0; i < state.net_commands.size(); ++i) {
        const net_command& command = state.net_commands[i];
        if (command.type == net_command_type::take_shot) {
            shot_at = i;
            CHECK(command.shot.shot_at == start_time / 1000);
        } else if (command.type == net_command_type::motion && at_ball(command.motion.mode)) {
            motion_at = std::min(motion_at, i);
        }
    }
    REQUIRE(shot_at < state.net_commands.size());
    CHECK(motion_at < shot_at);
}

TEST_CASE("my swing waits at its top for the server's answer, and the shot ends where it says") {
    game_state state = online_hub();
    on_hole(state);
    enter_addressing(state);
    update_game(state, swing_input(), 0.016f);
    REQUIRE(state.address.swing_elapsed.has_value());
    const club_definition& club = state.clubs[state.selected_club];
    const float top = swing_tempo_for(club, state.tuning.swing).back_seconds;
    wait(state, 3.0f);
    CHECK(state.mode == game_mode::addressing);
    CHECK(!shot_playing(state));
    CHECK(near(*state.address.swing_elapsed, top, 0.0001f));

    const glm::vec3 server_rest = state.ball->position + yaw_direction(state.player.yaw) * 30.0f;
    answer_my_swing(state, server_rest);
    swing_through(state);
    REQUIRE(shot_playing(state));
    play_out_shot(state);
    CHECK(near(state.ball->position, server_rest, 0.001f));
    CHECK(state.ball->strokes == 1);

    // A later answer within the correction distance moves nothing.
    const glm::vec3 rest = state.ball->position;
    state.online.shots.push_back(played_shot(me, 0, state.ball->strokes, shot_setup{}, rest + glm::vec3(0.01f, 0.0f, 0.0f), false));
    update_game(state, game_input{}, 0.1f);
    CHECK(near(state.ball->position, rest));
}

TEST_CASE("online the hole ends when the server says: scored when it holed my ball") {
    game_state state = online_hub();
    on_hole(state);
    // Holed here: the hole stays open until the server answers.
    const glm::vec3 cup = pin_anchor_position(state, state.ball->hole);
    const glm::vec3 stance = state.player.position;
    state.ball->strokes = 2;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball->position, cup}, {}});
    play_out_shot(state);
    CHECK(ball_in_play(state));
    CHECK(!hole_played(state.round, 0));

    state.online.shots.push_back(played_shot(me, 0, 2, shot_setup{}, cup, true));
    state.online.balls[me] = room_ball{0, cup, 2, true};
    update_game(state, game_input{}, 0.016f);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    REQUIRE(hole_played(state.round, 0));
    CHECK(*state.round.strokes[0] == 2);
    // The server leaves the player where they hit from, as offline.
    CHECK(near(horizontal(state.player.position), horizontal(stance), 0.001f));
}

TEST_CASE("online holing out keeps the view on the cup, and where I turned before the server answered") {
    game_state state = online_hub();
    on_hole(state);
    const glm::vec3 cup = pin_anchor_position(state, state.ball->hole);
    const glm::vec3 stance = state.player.position;
    state.player.yaw = wrap_angle(state.player.yaw + 1.0f);
    state.ball->strokes = 1;
    play_shot(state, shot_result{cup, true, 0.05f, {state.ball->position, cup}, {}});
    play_out_shot(state);
    CHECK(near(state.player.yaw, yaw_towards(state.player.position, cup), 0.0001f));

    state.player.yaw = wrap_angle(state.player.yaw + 0.3f);
    const float yaw = state.player.yaw;
    state.online.shots.push_back(played_shot(me, 0, 1, shot_setup{}, cup, true));
    state.online.balls[me] = room_ball{0, cup, 1, true};
    update_game(state, game_input{}, 0.016f);
    REQUIRE(hole_played(state.round, 0));
    CHECK(near(state.player.yaw, yaw, 0.0001f));
    CHECK(near(horizontal(state.player.position), horizontal(stance), 0.001f));
}

TEST_CASE("online a hole the server ends without holing my ball is given up, me where I stand") {
    game_state state = online_hub();
    on_hole(state);
    const glm::vec3 standing = state.player.position;
    state.online.balls.erase(me);
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.ball.has_value());
    CHECK(!hole_played(state.round, 0));
    CHECK(near(state.player.position, standing, 0.001f));
}

TEST_CASE("a refused setup ends the swing over the ball and says why") {
    game_state state = online_hub();
    on_hole(state);
    const glm::vec3 tee = state.ball->position;
    state.online.balls[me] = room_ball{0, tee, 0};
    enter_addressing(state);
    update_game(state, swing_input(), 0.016f);
    REQUIRE(state.address.swing_elapsed.has_value());
    refuse(state, "take_shot", "club_not_in_hand");
    CHECK(!shot_playing(state));
    CHECK(state.mode == game_mode::addressing);
    CHECK(!state.address.swing_elapsed.has_value());
    CHECK(state.ball->strokes == 0);
    CHECK(near(state.ball->position, tee));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == "online.error.club_not_in_hand");

    wait(state, state.tuning.net.notice_seconds);
    CHECK(!state.notice.has_value());
}

TEST_CASE("a refused tee-up or claim puts the player back where the server has them") {
    game_state state = online_hub();
    const glm::vec3 accepted = state.player.position;
    tee_up(state, 0, false);
    REQUIRE(state.ball.has_value());
    const net_command* teed = find_command(state, net_command_type::tee_up);
    REQUIRE(teed != nullptr);
    CHECK(teed->hole_index == 0U);
    CHECK(!teed->give_up);
    refuse(state, "tee_up", "too_far");
    CHECK(!state.ball.has_value());
    CHECK(near(state.player.position, accepted, 0.001f));

    state.player.position += glm::vec3(5.0f, 0.0f, 0.0f);
    refuse(state, "claim_collectible", "too_far");
    CHECK(near(state.player.position, accepted, 0.001f));
}

TEST_CASE("giving up at another tee goes to the server with the tee-up; refused, my ball is back as it has it") {
    game_state state = online_hub();
    on_hole(state);
    state.ball->strokes = 2;
    state.online.balls[me].stroke_count = 2;
    state.player.position = state.world.hole_starts[2].position;
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.give_up_prompt.has_value());
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 2U);
    const net_command* teed = find_command(state, net_command_type::tee_up);
    REQUIRE(teed != nullptr);
    CHECK(teed->hole_index == 2U);
    CHECK(teed->give_up);
    CHECK(!has_command(state, net_command_type::give_up_hole));

    refuse(state, "tee_up", "too_far");
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 0U);
    CHECK(state.ball->strokes == 2);
    CHECK(state.ball->place == ball_place::at_rest);
}

TEST_CASE("giving up from the menu tells the server; refused, my ball is back as it has it") {
    game_state state = online_hub();
    on_hole(state);
    give_up_hole(state);
    CHECK(!state.ball.has_value());
    CHECK(has_command(state, net_command_type::give_up_hole));
    refuse(state, "give_up_hole", "not_on_hole");
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->hole == 0U);
    CHECK(state.ball->place == ball_place::teed);
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
    CHECK(!tee_up(state, 0, false));
    CHECK(!state.ball.has_value());
    CHECK(state.net_commands.empty());
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_online_tee_in_use);

    state.online.balls[other].stroke_count = 1;
    CHECK(tee_up(state, 0, false));
    CHECK(state.ball.has_value());
}

TEST_CASE("offline, the tee is never in use") {
    game_state state = started_game(fixture_hub_course());
    state.online.balls[other] = room_ball{0, glm::vec3(0.0f), 0};
    CHECK(tee_up(state, 0, false));
}

TEST_CASE("moving too fast snaps back to the last accepted position, keeping my ball") {
    game_state state = online_hub();
    const glm::vec3 accepted = state.player.position;
    state.player.position += glm::vec3(30.0f, 0.0f, 0.0f);
    refuse(state, "update_motion", "too_fast");
    CHECK(near(state.player.position, accepted, 0.001f));

    // A tee-up the server has not answered yet is not undone.
    tee_up(state, 0, false);
    refuse(state, "update_motion", "too_fast");
    CHECK(ball_in_play(state));
}

TEST_CASE("G groups me with the nearest player wherever their ball and mine are played") {
    game_state state = online_hub();
    on_hole(state);
    const float reach = state.tuning.net.group_join_distance;
    add_other(state, glm::vec3(reach * 0.5f, 0.0f, 0.0f), 5);
    state.online.balls[other] = room_ball{2, state.player.position, 3, false};
    state.online.group_sizes[5] = 1;
    game_input g;
    g.group = true;
    update_game(state, g, 0.0f);
    const net_command* join = find_command(state, net_command_type::join_group);
    REQUIRE(join != nullptr);
    CHECK(join->group_id == 5U);
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

TEST_CASE("my jump goes out as I rise, and others see it as the same hop") {
    game_state state = online_hub();
    update_game(state, game_input{}, 0.02f);
    state.net_commands.clear();
    game_input jump;
    jump.jump = true;
    update_game(state, jump, 0.02f);
    REQUIRE(state.net_commands.size() == 1U);
    const net_motion sent = state.net_commands.back().motion;
    CHECK(sent.position.y - terrain_height(state.area, sent.position) > airborne_height);
    // Once in the air, not again for the rest of the hop.
    state.net_commands.clear();
    update_game(state, game_input{}, 0.02f);
    CHECK(state.net_commands.empty());

    game_state seen = online_hub();
    add_other(seen, glm::vec3(0.0f), 0);
    room_player& player = seen.online.players[other];
    seen.online.server_now += server_micros(1.0);
    player.motion = sent;
    player.motion_at = seen.online.server_now;
    const player_tuning& tuning = seen.tuning.player;
    const auto lift = [&] {
        const glm::vec3 shown = seen.remote_avatars.at(other).position;
        return shown.y - terrain_height(seen.area, shown);
    };
    update_remote_players(seen, 0.0f);
    CHECK(near(lift(), 0.0f, 0.001f));
    update_remote_players(seen, 0.5f * hop_seconds(tuning));
    CHECK(near(lift(), tuning.jump_speed * tuning.jump_speed / (2.0f * tuning.jump_gravity), 0.001f));
    update_remote_players(seen, 0.5f * hop_seconds(tuning));
    CHECK(near(lift(), 0.0f, 0.001f));
    CHECK(seen.remote_avatars.at(other).vertical.grounded);
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
    const course_hole& marker = state.holes[0];
    shot_setup input;
    input.ball_start = resting_ball_position(state.area, marker.tee_position, state.tuning.scale.ball_physics_radius_meters);
    input.aim_angle = yaw_towards(marker.tee_position, marker.pin_position);
    input.club_id = state.clubs.front().id;
    input = stocked(input, state, 0.8f);
    const glm::vec3 rest = input.ball_start + glm::vec3(3.0f, 0.0f, 3.0f);
    state.online.balls[other] = room_ball{0, rest, 1};
    state.online.shots.push_back(played_shot(other, 0, 1, input, rest, false));
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

TEST_CASE("another player's item use from the server plays on their avatar, as mine does") {
    game_state state = online_hub();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    const std::string cigarette = state.default_loadout.smokable;
    const std::string beer = state.default_loadout.drinkable;
    state.online.item_uses.push_back(room_item_use{other, cigarette});
    state.online.item_uses.push_back(room_item_use{7, beer});  // nobody shown here
    update_game(state, game_input{}, 0.0f);
    CHECK(state.online.item_uses.empty());
    const remote_avatar& avatar = state.remote_avatars.at(other);
    REQUIRE(avatar.smoking.has_value());
    CHECK(avatar.smoking->item == cigarette);
    CHECK(!avatar.drinking.has_value());

    update_game(state, game_input{}, 0.02f);
    CHECK(state.remote_avatars.at(other).smoking->elapsed > 0.0f);
    state.online.item_uses.push_back(room_item_use{other, beer});
    const float smoke_seconds = state.items.find(cigarette)->use->seconds_per_use;
    wait(state, smoke_seconds * 0.5f);
    CHECK(state.remote_avatars.at(other).smoking.has_value());
    CHECK(state.remote_avatars.at(other).drinking.has_value());
    wait(state, smoke_seconds + state.items.find(beer)->use->seconds_per_use);
    CHECK(!state.remote_avatars.at(other).smoking.has_value());
    CHECK(!state.remote_avatars.at(other).drinking.has_value());
}

TEST_CASE("another player addresses their ball and swings with their meter, their shot leaving as it arrives") {
    game_state state = online_hub();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    state.online.item_uses.push_back(room_item_use{other, state.default_loadout.drinkable});
    update_game(state, game_input{}, 0.0f);
    REQUIRE(state.remote_avatars.at(other).drinking.has_value());
    const course_hole& marker = state.holes[0];
    shot_setup input;
    input.ball_start = resting_ball_position(state.area, marker.tee_position, state.tuning.scale.ball_physics_radius_meters);
    input.aim_angle = yaw_towards(marker.tee_position, marker.pin_position);
    input.club_id = state.clubs.front().id;
    input = stocked(input, state, 0.8f);
    state.online.balls[other] = room_ball{0, input.ball_start, 0};
    room_player& player = state.online.players.at(other);
    player.motion->yaw = input.aim_angle;
    CHECK(!remote_address_pose(state, other).has_value());  // walking
    // Addressing it: the club down at the ball.
    player.motion->mode = motion_mode::address;
    std::optional<remote_address> address = remote_address_pose(state, other);
    REQUIRE(address.has_value());
    CHECK(near(address->ball_position, input.ball_start));
    CHECK(address->aim_angle == input.aim_angle);
    CHECK(address->club_raise == 0.0f);

    // Swinging: the club goes back on a full swing's tempo from when the server heard it.
    player.motion->mode = motion_mode::swing;
    player.motion_at = state.online.server_now;
    state.online.server_now += server_micros(0.3);
    address = remote_address_pose(state, other);
    REQUIRE(address.has_value());
    const swing_tempo tempo{state.tuning.swing.backswing_seconds, state.tuning.swing.downswing_seconds};
    CHECK(near(address->club_raise, swing_club_raise(0.3f, 1.0f, tempo), 0.001f));
    CHECK(address->club_raise > 0.1f);

    // The shot leaves as it arrives, and its swing is over.
    const glm::vec3 rest = input.ball_start + glm::vec3(3.0f, 0.0f, 3.0f);
    state.online.balls[other].position = rest;
    state.online.shots.push_back(played_shot(other, 0, 1, input, rest, false));
    update_game(state, game_input{}, 0.0f);
    CHECK(!remote_address_pose(state, other).has_value());
    // Hitting it doesn't end their emotes: the server and their own game don't either.
    CHECK(state.remote_avatars.at(other).drinking.has_value());
    wait(state, 0.1f);
    std::vector<shown_ball> balls = remote_balls(state);
    REQUIRE(balls.size() == 1U);
    CHECK(!near(balls[0].position, input.ball_start, 0.01f));

    // Their next address shows the club again.
    player.motion->mode = motion_mode::address;
    player.motion_at = state.online.server_now;
    CHECK(remote_address_pose(state, other).has_value());
}

TEST_CASE("my swing tells the server it started, where I stand, then my setup") {
    game_state state = online_hub();
    on_hole(state);
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    CHECK(current_motion(state).mode == motion_mode::address);
    CHECK(current_motion(state).turn_rate == 0.0f);

    state.net_commands.clear();
    update_game(state, swing_input(), 0.0f);
    REQUIRE(state.address.swing_elapsed.has_value());
    REQUIRE(state.net_commands.size() == 2U);
    CHECK(state.net_commands[0].type == net_command_type::motion);
    CHECK(state.net_commands[0].motion.mode == motion_mode::swing);
    // Then my setup, as I set it up, its next stroke.
    const net_command& shot = state.net_commands[1];
    CHECK(shot.type == net_command_type::take_shot);
    CHECK(shot.stroke == 1);
    CHECK(shot.shot.face_open == state.address.setup.face_open);
    CHECK(shot.shot.backswing == state.address.setup.backswing);
    CHECK(shot.flags_out.empty());
}

TEST_CASE("the scorecard has a row per member of my group from the server") {
    game_state state = online_hub();
    state.online.players[me].group_id = 3;
    state.round = complete_hole(state.round, 0, state.course_holes[0].par + 1);
    add_other(state, glm::vec3(0.0f), 3);
    state.online.players[other].round_strokes = std::vector<int>(state.course_holes.size(), 0);
    room_player stranger = state.online.players[other];
    stranger.group_id = 0;
    state.online.players[9] = stranger;

    const std::vector<scorecard_player> rows = build_scorecard_data(state, shipped_text_assets().strings).players;
    REQUIRE(rows.size() == 2U);
    CHECK(rows[0].me);
    CHECK(rows[0].name == "ME");
    CHECK(rows[0].strokes[0] == state.course_holes[0].par + 1);
    CHECK(rows[0].relative_label == "+1");
    CHECK(rows[1].name == "OTHER");
    CHECK(!rows[1].me);
    CHECK(rows[1].total_strokes == 0);

    // Outside a group, only mine.
    state.online.players[me].group_id = 0;
    const std::vector<scorecard_player> alone = build_scorecard_data(state, shipped_text_assets().strings).players;
    REQUIRE(alone.size() == 1U);
    CHECK(!alone[0].me);
    CHECK(alone[0].total_strokes == state.course_holes[0].par + 1);
}

TEST_CASE("a finished round goes on to the next where the player stands") {
    game_state state = online_hub();
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        tee_up(state, i, false);
        state.ball->strokes = 3;
        complete_current_hole(state);
        pick_up_from_cup(state);
    }
    REQUIRE(round_finished(state.round));
    const glm::vec3 standing = state.player.position;
    REQUIRE(horizontal_distance(standing, state.holes.back().tee_position) < 5.0f);
    start_next_round(state);
    CHECK(!round_finished(state.round));
    CHECK(!ball_in_play(state));
    CHECK(near(horizontal(state.player.position), horizontal(standing), 0.001f));
}

namespace {
// Grouped with the other player, every hole holed in 3, the last while they
// still play it, my ball left in its cup there. Returns the last hole.
std::size_t finished_before_group(game_state& state) {
    state.online.players[me].group_id = 3;
    add_other(state, glm::vec3(4.0f, 0.0f, 0.0f), 3);
    const std::size_t last = state.course_holes.size() - 1U;
    state.online.players[other].round_strokes = std::vector<int>(state.course_holes.size(), 0);
    state.online.players[other].round_strokes[0] = 5;
    state.online.balls[other] = room_ball{static_cast<int>(last), state.holes[last].pin_position, 3, false};
    for (std::size_t i = 0; i <= last; ++i) {
        pick_up_from_cup(state);
        tee_up(state, i, false);
        state.ball->strokes = 3;
        complete_current_hole(state);
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
    const course_hole& marker = state.holes[last];
    shot_setup input;
    input.ball_start = resting_ball_position(state.area, marker.pin_position + glm::vec3(2.0f, 0.0f, 0.0f), state.tuning.scale.ball_physics_radius_meters);
    input.aim_angle = yaw_towards(input.ball_start, marker.pin_position);
    input.club_id = state.clubs.back().id;
    input = stocked(input, state, 0.3f);
    state.online.shots.push_back(played_shot(other, static_cast<int>(last), 4, input, marker.pin_position, true));
    state.online.balls[other] = room_ball{static_cast<int>(last), marker.pin_position, 4, true};
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
    const std::vector<scorecard_player> rows = build_scorecard_data(state, shipped_text_assets().strings).players;
    REQUIRE(rows.size() == 2U);
    CHECK(rows[0].strokes == std::vector<int>(state.course_holes.size(), 3));
    CHECK(rows[0].total_strokes == 3 * static_cast<int>(state.course_holes.size()));
    CHECK(std::count_if(rows[1].strokes.begin(), rows[1].strokes.end(), [](const int strokes) { return strokes > 0; }) == 2);
    CHECK(rows[1].total_strokes == 9);

    start_next_round(state);
    CHECK(state.group_strokes.empty());
}

TEST_CASE("waiting for the group I can walk, but play nothing") {
    game_state state = online_hub();
    finished_before_group(state);
    REQUIRE(waiting_for_group(state));
    state.net_commands.clear();
    const glm::vec3 before = state.player.position;

    game_input walk;
    walk.forward_held = true;
    walk.interact = true;
    walk.swing = true;
    for (int i = 0; i < 30; ++i) {
        update_game(state, walk, 0.05f);
    }
    CHECK(horizontal_distance(state.player.position, before) > 1.0f);
    CHECK(state.mode == game_mode::walking);
    const auto motion = std::find_if(state.net_commands.begin(), state.net_commands.end(),
                                     [](const net_command& command) { return command.type == net_command_type::motion; });
    REQUIRE(motion != state.net_commands.end());
}

TEST_CASE("results wait for nobody who gave up the hole or left the group") {
    game_state state = online_hub();
    const std::size_t last = finished_before_group(state);
    REQUIRE(waiting_for_group(state));
    const room_ball playing = state.online.balls[other];
    state.online.balls.erase(other);  // gave up
    CHECK(round_results_shown(state));

    state.online.balls[other] = playing;
    (void)last;
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
    state.online.balls[other] = room_ball{static_cast<int>(state.course_holes.size() - 1U), glm::vec3(0.0f), 1, false};
    for (std::size_t i = 0; i < state.course_holes.size(); ++i) {
        tee_up(state, i, false);
        state.ball->strokes = 3;
        complete_current_hole(state);
        pick_up_from_cup(state);
    }
    CHECK(round_results_shown(state));

    game_state offline = started_game(fixture_hub_course());
    for (std::size_t i = 0; i < offline.course_holes.size(); ++i) {
        tee_up(offline, i, false);
        offline.ball->strokes = 3;
        complete_current_hole(offline);
        pick_up_from_cup(offline);
    }
    CHECK(!waiting_for_group(offline));
    CHECK(round_results_shown(offline));
}

namespace {
// Holed hole 0, my ball in its cup here and on the server; at the cup.
void holed_at_cup(game_state& state) {
    on_hole(state);
    state.ball->strokes = 3;
    complete_current_hole(state);
    REQUIRE(state.ball.has_value());
    REQUIRE(state.ball->place == ball_place::in_cup);
    state.online.balls[me] = room_ball{0, state.ball->position, 3, true};
    state.player.position = state.ball->position;
    state.online.players[me].motion = current_motion(state);
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
    update_game(state, interact_input(), 0.0f);
    CHECK(!state.ball.has_value());
    CHECK(sent(state, net_command_type::pick_up_ball));

    state.online.refusals.push_back(reducer_failure{"pick_up_ball", "too_far"});
    update_game(state, game_input{}, 0.0f);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == online_error_text_key("too_far"));
}

TEST_CASE("online a tee-up refused for the ball in the cup puts it back there") {
    game_state state = online_hub();
    holed_at_cup(state);
    update_game(state, interact_input(), 0.0f);
    REQUIRE(!state.ball.has_value());

    REQUIRE(tee_up(state, 1, false));
    state.online.refusals.push_back(reducer_failure{"tee_up", "ball_in_cup"});
    update_game(state, game_input{}, 0.0f);
    REQUIRE(state.ball.has_value());
    CHECK(state.ball->place == ball_place::in_cup);
    CHECK(state.ball->hole == 0U);
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == text_ball_in_cup);
}

TEST_CASE("online my cart moves to where the server parks it, once") {
    game_state state = online_hub();
    const glm::vec3 parked = state.player.position + glm::vec3(20.0f, 0.0f, 4.0f);
    state.online.carts[me] = room_cart{parked, 1.0f, 0};
    update_game(state, game_input{}, 0.02f);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(parked), 0.0001f));
    CHECK(near(state.cart.pose.yaw, 1.0f));
    CHECK(state.cart.placed_by_server);

    // From then on my cart is where I leave it, whatever my row says.
    state.online.carts[me].position = parked + glm::vec3(5.0f, 0.0f, 0.0f);
    update_game(state, game_input{}, 0.02f);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(parked), 0.0001f));
}

TEST_CASE("online I ride in another's free passenger seat with the server, sat where their cart is shown, and step out") {
    game_state state = online_hub();
    add_other(state, glm::vec3(30.0f, 0.0f, 0.0f), 0);
    park_other_cart(state, glm::vec3(30.0f, 0.0f, 0.0f));
    beside_other_seat(state);
    state.net_commands.clear();

    const std::optional<reachable_seat> seat = nearest_seat(state);
    REQUIRE(seat.has_value());
    CHECK(seat->owner == other);
    CHECK(seat->seat == cart_seat::passenger);

    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.ride.has_value());
    CHECK(state.ride->owner == other);
    // Where I stand first, then the seat: the server checks the reach.
    REQUIRE(state.net_commands.size() >= 2U);
    CHECK(state.net_commands[0].type == net_command_type::motion);
    const net_command* board = find_command(state, net_command_type::board_cart);
    REQUIRE(board != nullptr);
    CHECK(board->cart_owner == other);

    state.online.carts[other].passenger = me;
    play_cart_step(state);
    CHECK(state.ride->confirmed);
    CHECK(!state.ride->step.has_value());
    const cart_pose cart = *shown_cart(state, other);
    CHECK(near(horizontal(state.player.position), horizontal(seat_position(cart, cart_seat::passenger, state.tuning.cart)),
               0.0001f));
    CHECK(current_motion(state).mode == motion_mode::ride);
    CHECK(!driving_cart(state));

    // Its driver takes the wheel and drives it on: I go with it.
    room_player& driver = state.online.players[other];
    driver.motion->mode = motion_mode::cart;
    driver.motion->position = seat_position(cart, cart_seat::driver, state.tuning.cart) + glm::vec3(0.0f, 0.0f, 6.0f);
    driver.motion_at = state.online.server_now + 1;
    for (int i = 0; i < 30; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    const cart_pose moved = *shown_cart(state, other);
    CHECK(horizontal_distance(moved.position, cart.position) > 5.0f);
    CHECK(near(horizontal(state.player.position), horizontal(seat_position(moved, cart_seat::passenger, state.tuning.cart)),
               0.001f));

    state.net_commands.clear();
    update_game(state, interact_input(), 0.02f);
    CHECK(has_command(state, net_command_type::leave_cart));
    state.online.carts[other].passenger = 0;
    play_cart_step(state);
    CHECK(on_foot(state));
}

TEST_CASE("online a taken passenger seat is not offered, nor my own cart's") {
    game_state state = online_hub();
    park_other_cart(state, glm::vec3(30.0f, 0.0f, 0.0f));
    beside_other_seat(state);
    REQUIRE(nearest_seat(state).has_value());
    state.online.carts[other].passenger = 3;
    CHECK(!nearest_seat(state).has_value());

    // My own cart in my room: only its wheel, never its passenger seat.
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    state.player.position = anchor_on_terrain(state.area, step_out_position(state.cart.pose, cart_seat::passenger, state.tuning.cart));
    const std::optional<reachable_seat> mine = nearest_seat(state);
    REQUIRE(mine.has_value());
    CHECK(mine->owner == 0U);
    CHECK(mine->seat == cart_seat::driver);
}

TEST_CASE("online a refused seat stands me up and says why") {
    game_state state = online_hub();
    park_other_cart(state, glm::vec3(30.0f, 0.0f, 0.0f));
    beside_other_seat(state);
    update_game(state, interact_input(), 0.02f);
    REQUIRE(state.ride.has_value());

    refuse(state, reducer_board_cart, error_seat_taken);
    CHECK(on_foot(state));
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == online_error_text_key(error_seat_taken));
}

TEST_CASE("online I am out of another's cart once the server no longer seats me there") {
    game_state state = online_hub();
    park_other_cart(state, glm::vec3(30.0f, 0.0f, 0.0f));
    beside_other_seat(state);
    update_game(state, interact_input(), 0.02f);
    state.online.carts[other].passenger = me;
    play_cart_step(state);
    REQUIRE(state.ride.has_value());
    REQUIRE(state.ride->confirmed);

    // Its driver left the room, and the cart with them.
    state.net_commands.clear();
    state.online.carts.erase(other);
    update_game(state, game_input{}, 0.02f);
    CHECK(on_foot(state));
    CHECK(!has_command(state, net_command_type::leave_cart));
}

TEST_CASE("online my cart going over tells the server where and how, and a refusal stands it back on its wheels") {
    game_state state = online_hub();
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0};
    sit_in_cart(state);
    state.net_commands.clear();
    // A pond deeper than its wade depth right under it.
    state.area.zones.push_back(circle_zone(material_zone_type::water, state.cart.pose.position, 6.0f));
    state.area.water_levels.push_back(state.cart.pose.position.y + 2.0f * my_cart_spec(state).wade_depth_m);
    update_game(state, game_input{}, 0.05f);
    REQUIRE(state.cart.motion.condition == cart_condition::sunk);
    CHECK(on_foot(state));
    // Where it went in first (its last motion at the wheel), then what happened.
    const net_command* motion = find_command(state, net_command_type::motion);
    const net_command* overturn = find_command(state, net_command_type::overturn_cart);
    REQUIRE(motion != nullptr);
    REQUIRE(overturn != nullptr);
    CHECK(motion < overturn);
    CHECK(motion->motion.mode == motion_mode::cart);
    CHECK(overturn->overturned_to == cart_condition::sunk);
    // Online the server tows it, not the game.
    for (int i = 0; i < static_cast<int>(my_cart_spec(state).tow_seconds / 0.05f) + 10; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.cart.motion.condition == cart_condition::sunk);

    refuse(state, reducer_overturn_cart, error_cart_steady);
    CHECK(state.cart.motion.condition == cart_condition::upright);
}

TEST_CASE("online my cart stands up again where the server tows it, and another's tipped cart is righted through the server") {
    game_state state = online_hub();
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, 0, cart_condition::sunk};
    state.cart.placed_by_server = true;
    state.cart.motion.condition = cart_condition::sunk;
    update_game(state, game_input{}, 0.02f);
    CHECK(state.cart.motion.condition == cart_condition::sunk);
    const glm::vec3 spot = state.player.position + glm::vec3(-12.0f, 0.0f, 3.0f);
    state.online.carts[me] = room_cart{spot, 0.5f, 0, cart_condition::upright};
    update_game(state, game_input{}, 0.02f);
    CHECK(state.cart.motion.condition == cart_condition::upright);
    CHECK(near(horizontal(state.cart.pose.position), horizontal(spot), 0.0001f));

    // Another's cart on its side: no seat, but F beside it rights it.
    park_other_cart(state, glm::vec3(30.0f, 0.0f, 0.0f));
    state.online.carts[other].condition = cart_condition::on_left_side;
    beside_other_seat(state);
    CHECK(!nearest_seat(state).has_value());
    const std::optional<interactable> near_me = nearest_interactable(state);
    REQUIRE(near_me.has_value());
    REQUIRE(near_me->kind == interactable_kind::tipped_cart);
    CHECK(near_me->tipped.owner == other);
    state.net_commands.clear();
    update_game(state, interact_input(), 0.02f);
    const float right_seconds = cart_spec_of(state, state.default_loadout.cart).right_seconds;
    for (int i = 0; i < static_cast<int>(right_seconds / 0.05f) + 2; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    const net_command* right = find_command(state, net_command_type::right_cart);
    REQUIRE(right != nullptr);
    CHECK(right->cart_owner == other);
}

TEST_CASE("online the server refusing my wheel stands me up beside my cart") {
    game_state state = online_hub();
    sit_in_cart(state);
    refuse(state, reducer_update_motion, error_too_far);
    CHECK(on_foot(state));
}

TEST_CASE("another player riding in my cart sits in its passenger seat as I drive it") {
    game_state state = online_hub();
    add_other(state, glm::vec3(3.0f, 0.0f, 0.0f), 0);
    sit_in_cart(state);
    state.online.carts[me] = room_cart{state.cart.pose.position, state.cart.pose.yaw, other};
    room_player& rider = state.online.players[other];
    rider.motion->mode = motion_mode::ride;
    rider.motion_at = state.online.server_now + 1;
    game_input drive;
    drive.forward_held = true;
    for (int i = 0; i < 20; ++i) {
        update_game(state, drive, 0.05f);
    }
    // After the step's blend, glued to the seat.
    const remote_avatar& shown = state.remote_avatars.at(other);
    CHECK(near(horizontal(shown.position), horizontal(seat_position(state.cart.pose, cart_seat::passenger, state.tuning.cart)),
               0.001f));
    CHECK(near(shown.yaw, state.cart.pose.yaw, 0.0001f));
}

TEST_CASE("online every green's flag is the room's: P asks the server, and the rows decide") {
    game_state state = online_hub();
    on_hole(state);
    const glm::vec3 cup = state.area.cups[0].centre;
    state.player.position = resting_on_terrain(state, cup + glm::vec3(1.0f, 0.0f, 0.0f));
    REQUIRE(flag_in_reach(state).has_value());
    game_input p;
    p.toggle_flag = true;
    update_game(state, p, 0.016f);
    const net_command* asked = find_command(state, net_command_type::set_flag);
    REQUIRE(asked != nullptr);
    CHECK(asked->hole_index == 0U);
    CHECK(asked->flag_out);
    CHECK(state.flags_in[0]);  // not until the server says

    state.online.flags_out = {0};
    update_game(state, game_input{}, 0.016f);
    CHECK(!state.flags_in[0]);
    // And my next swing sends the flags as I have them.
    state.net_commands.clear();
    state.player.position = state.ball->position;
    enter_addressing(state);
    update_game(state, swing_input(), 0.016f);
    const net_command* shot = find_command(state, net_command_type::take_shot);
    REQUIRE(shot != nullptr);
    CHECK((shot->flags_out == std::vector<int>{0}));
}

TEST_CASE("online my bag and club start as my row has them; moves and clubs are sent, and refusals put them back") {
    game_state state = online_hub();
    const club_definition& iron = *std::find_if(state.clubs.begin(), state.clubs.end(),
                                                [](const club_definition& club) { return club.label == "7I"; });
    state.online.bags[me] = room_bag{bag_place{bag_where::on_cart, cart_seat::driver, glm::vec3(0.0f), 0.0f}, iron.id};
    update_game(state, game_input{}, 0.016f);
    CHECK(state.clubs[state.selected_club].id == iron.id);
    CHECK(state.bag.where == bag_where::on_cart);

    state.player.position = bag_position(state);
    state.net_commands.clear();
    game_input b;
    b.move_bag = true;
    update_game(state, b, 0.016f);
    const net_command* moved = find_command(state, net_command_type::place_bag);
    REQUIRE(moved != nullptr);
    CHECK(moved->bag.where == bag_where::on_back);
    CHECK(state.bag.where == bag_where::on_back);

    refuse(state, "place_bag", "bag_out_of_reach");
    CHECK(state.bag.where == bag_where::on_cart);
    REQUIRE(state.notice.has_value());
    CHECK(state.notice->text_key == "online.error.bag_out_of_reach");

    // Taking a club from the bag on the cart is sent.
    update_game(state, interact_input(), 0.016f);
    REQUIRE(state.picker.has_value());
    game_input longer;
    longer.picker_steps = 1;
    update_game(state, longer, 0.016f);
    state.net_commands.clear();
    update_game(state, interact_input(), 0.016f);
    const net_command* taken = find_command(state, net_command_type::take_club);
    REQUIRE(taken != nullptr);
    CHECK(taken->club_id == state.clubs[state.selected_club].id);
    refuse(state, "take_club", "bag_out_of_reach");
    CHECK(state.clubs[state.selected_club].id == iron.id);
}

TEST_CASE("online my player row keeps my hand and natural shape, each change sent once") {
    game_state state = online_hub();
    state.online.players[me].natural_shape = natural_shape_unset;
    CHECK(!natural_shape_picked(state));
    record_natural_shape(state, natural_shape::draw);
    const net_command* picked = find_command(state, net_command_type::set_golfer);
    REQUIRE(picked != nullptr);
    CHECK(picked->natural == static_cast<std::uint8_t>(natural_shape_index(natural_shape::draw)));
    state.online.players[me].natural_shape = picked->natural;
    CHECK(natural_shape_picked(state));
    CHECK(my_golfer(state).natural == natural_shape::draw);

    state.net_commands.clear();
    state.hand = handedness::left;
    update_game(state, game_input{}, 0.016f);
    const net_command* hand = find_command(state, net_command_type::set_golfer);
    REQUIRE(hand != nullptr);
    CHECK(hand->hand == handedness::left);
    CHECK(hand->natural == state.online.players[me].natural_shape);
    state.net_commands.clear();
    update_game(state, game_input{}, 0.016f);
    CHECK(find_command(state, net_command_type::set_golfer) == nullptr);
}

TEST_CASE("a room shot replays here bit for bit as the server played it") {
    game_state state = online_hub();
    on_hole(state);
    const club_definition& club = state.clubs.front();
    shot_setup setup = stock_swing(club, state.tuning, 0.9f, lie_at(state.area, state.ball->position), state.player.yaw);
    setup.ball_start = state.ball->position;
    setup.shot_at = 1800006000000;
    room_shot shot = played_shot(other, 0, 1, setup, glm::vec3(0.0f), false);
    shot.record.seed = 1234;
    shot.record.snapshot = {snapshot_entry{snapshot_kind::person, state.player.position, 0.0f, false}};
    shot.golfer.natural = natural_shape::fade;
    shot.golfer.spread_scale = 0.7f;
    const shot_result here = room_shot_result(state, shot);
    shot_course course = current_shot_course(state);
    course.weather = course_weather_at(state, setup.shot_at);
    const shot_result direct = simulate_struck_shot(shot.record, shot.golfer, course, state.tuning, state.clubs);
    CHECK(here.rest_position == direct.rest_position);
    CHECK(here.trajectory == direct.trajectory);
}
