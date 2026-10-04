#include "doctest.h"

#include "game/course_session.h"
#include "game/game_state.h"
#include "game/mode_dispatch.h"
#include "game/motion_sync.h"
#include "game/net_types.h"
#include "game/progression.h"
#include "game/round_state.h"
#include "game/save_data.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include <glm/trigonometric.hpp>

namespace {
// A state that has already earned some offline progress, so a stray write to
// it would show.
game_state in_mode(game_state state, const play_mode play) {
    state.play = play;
    add_skill_xp(state.save.skills, "fitness", 7);
    return state;
}

std::string json_of(const save_data& save) {
    return save_data_to_json(save);
}

std::vector<net_command_type> command_types(const game_state& state) {
    std::vector<net_command_type> types;
    for (const net_command& command : state.net_commands) {
        types.push_back(command.type);
    }
    return types;
}

// Commands other than motion, which online play sends whenever it moves.
std::vector<net_command> gameplay_commands(const game_state& state) {
    std::vector<net_command> commands;
    for (const net_command& command : state.net_commands) {
        if (command.type != net_command_type::motion) {
            commands.push_back(command);
        }
    }
    return commands;
}

struct mode_pair {
    game_state offline;
    game_state online;
};

// Runs `act` on the same start in both modes, then checks the separation
// rules: offline never pushes commands or touches online progress, and online
// never changes the offline save or asks for it to be written.
mode_pair run_both(const game_state& start, const std::function<void(game_state&)>& act) {
    mode_pair pair{in_mode(start, play_mode::offline), in_mode(start, play_mode::online)};
    const std::string offline_save = json_of(pair.offline.save);
    const std::string online_progress = json_of(pair.offline.online.progress);
    act(pair.offline);
    act(pair.online);

    CHECK(pair.offline.net_commands.empty());
    CHECK(json_of(pair.offline.online.progress) == online_progress);
    CHECK(json_of(pair.online.save) == offline_save);
    CHECK(!pair.online.save_requested);
    CHECK(json_of(pair.online.online.progress) == online_progress);
    return pair;
}

game_input walk_forward() {
    game_input input;
    input.forward_held = true;
    return input;
}

net_tuning test_net_tuning() {
    net_tuning tuning;
    tuning.motion_heartbeat_seconds = 2.0f;
    tuning.motion_min_interval_seconds = 0.25f;
    tuning.motion_correction_distance = 1.0f;
    return tuning;
}
}

TEST_CASE("a shot gives swing xp offline and sends take_shot online") {
    const mode_pair pair = run_both(started_hole(), [](game_state& state) { hit_selected_club(state); });

    CHECK(skill_xp(pair.offline.save.skills, pair.offline.rewards.shot.skill_id) >= pair.offline.rewards.shot.xp);
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0].type == net_command_type::take_shot);
    CHECK(sent[0].stroke == 1);
    CHECK(sent[0].shot.club_id == pair.online.clubs[pair.online.selected_club].id);
    CHECK(pair.online.shot.has_value());
    CHECK(pair.online.stroke_count == 1);
}

TEST_CASE("smoking gives xp offline and sends the emote online; drinking only sends it") {
    game_input both;
    both.smoke = true;
    both.drink = true;
    const mode_pair pair = run_both(started_hole(), [&both](game_state& state) { update_game(state, both, 0.016f); });

    CHECK(skill_xp(pair.offline.save.skills, pair.offline.rewards.smoke.skill_id) >= pair.offline.rewards.smoke.xp);
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 2U);
    CHECK(sent[0].type == net_command_type::emote);
    CHECK(sent[0].emote == emote_id::smoke);
    CHECK(sent[1].emote == emote_id::drink);
    CHECK(pair.online.smoke_emote.active);
    CHECK(pair.online.cigarette_seconds_left > 0.0f);
}

TEST_CASE("walking earns movement xp offline only") {
    const mode_pair pair = run_both(started_hole(), [](game_state& state) {
        for (int i = 0; i < 40; ++i) {
            update_game(state, walk_forward(), 0.05f);
        }
    });

    CHECK(skill_xp(pair.offline.save.skills, pair.offline.rewards.walking.skill_id) > 7);
    CHECK(gameplay_commands(pair.online).empty());
    CHECK(!pair.online.net_commands.empty());
}

TEST_CASE("driving on a road earns cart xp offline only") {
    game_state start = started_game(fixture_hub_course());
    const std::vector<glm::vec3>& road = start.hub->world.cart_roads.front().polyline;
    start.player.position = road.front();
    start.player.yaw = yaw_towards(road[0], road[1]);
    game_input drive;
    drive.cart_held = true;

    const mode_pair pair = run_both(start, [&drive](game_state& state) {
        for (int i = 0; i < 80; ++i) {
            drive.action = i % 6 == 0;
            update_game(state, drive, 0.05f);
        }
    });

    CHECK(skill_xp(pair.offline.save.skills, pair.offline.rewards.cart_on_road.skill_id) > 0);
    CHECK(skill_xp(pair.offline.save.skills, pair.offline.rewards.drift_on_road.skill_id) > 0);
    CHECK(gameplay_commands(pair.online).empty());
}

TEST_CASE("a collectible is claimed on the save offline and sent online") {
    game_state start = started_game(fixture_hub_course());
    const course_world_collectible lost_ball = start.hub->world.collectibles[1];
    start.player.position = lost_ball.position;

    const mode_pair pair = run_both(start, [](game_state& state) { update_game(state, action_input(), 0.016f); });

    CHECK(pair.offline.save.collected_ids == std::vector<std::string>{lost_ball.id});
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0].type == net_command_type::claim_collectible);
    CHECK(sent[0].collectible_id == lost_ball.id);
}

TEST_CASE("an online claim is not sent again until the server answers") {
    game_state state = in_mode(started_game(fixture_hub_course()), play_mode::online);
    const course_world_collectible lost_ball = state.hub->world.collectibles[1];
    state.player.position = lost_ball.position;

    update_game(state, action_input(), 0.016f);
    update_game(state, action_input(), 0.016f);
    CHECK(gameplay_commands(state).size() == 1U);
    CHECK(state.online.pending_claims == std::vector<std::string>{lost_ball.id});

    // Other progress (movement XP, say) is not the claim's answer.
    receive_online_progress(state, state.online.progress);
    CHECK(state.online.pending_claims == std::vector<std::string>{lost_ball.id});
    update_game(state, action_input(), 0.016f);
    CHECK(gameplay_commands(state).size() == 1U);

    // Its answer is the collectible showing up claimed.
    save_data answered = state.online.progress;
    answered.collected_ids.push_back(lost_ball.id);
    receive_online_progress(state, answered);
    CHECK(state.online.pending_claims.empty());
}

TEST_CASE("starting a hub hole sends enter_hole online") {
    game_state start = started_game(fixture_hub_course());
    start.player.position = start.hub->markers[1].start_position;

    const mode_pair pair = run_both(start, [](game_state& state) { update_game(state, action_input(), 0.016f); });

    CHECK(pair.offline.hole.has_value());
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0].type == net_command_type::enter_hole);
    CHECK(sent[0].hole_index == 1U);
    CHECK(pair.online.hole.has_value());
}

TEST_CASE("every hole entered sends enter_hole online, hub or not") {
    const mode_pair pair = run_both(started_hole(), [](game_state& state) {
        state.stroke_count = 2;
        complete_current_hole(state);
    });

    REQUIRE(pair.online.hole.has_value());
    CHECK(pair.online.hole->index == 1U);
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0].type == net_command_type::enter_hole);
    CHECK(sent[0].hole_index == 1U);
}

TEST_CASE("a retee is sent online only") {
    const mode_pair pair = run_both(started_hole(), [](game_state& state) {
        game_input retee;
        retee.retee = true;
        update_game(state, retee, 0.016f);
    });
    const std::vector<net_command> sent = gameplay_commands(pair.online);
    REQUIRE(sent.size() == 1U);
    CHECK(sent[0].type == net_command_type::retee);
}

TEST_CASE("completing a hole and the course is saved offline only") {
    const mode_pair pair = run_both(started_game(fixture_course({"test"})), [](game_state& state) {
        state.stroke_count = 2;
        complete_current_hole(state);
    });

    CHECK(round_finished(pair.offline.round));
    CHECK(pair.offline.save.holes_completed == 1);
    CHECK(pair.offline.save.completed_course_ids == std::vector<std::string>{"fixture_course"});
    CHECK(pair.offline.save_requested);
    CHECK(round_finished(pair.online.round));
    CHECK(gameplay_commands(pair.online).empty());
}

TEST_CASE("no motion is sent for a round that just finished") {
    game_state state = in_mode(started_game(fixture_course({"test"})), play_mode::online);
    state.stroke_count = 2;
    complete_current_hole(state);
    REQUIRE(round_finished(state.round));
    const std::size_t before = state.net_commands.size();

    // The server has the player in the hub: a motion on the last hole would be refused.
    sync_motion(state, 5.0f);
    send_motion_now(state);
    CHECK(state.net_commands.size() == before);
}

TEST_CASE("each mode shows and checks its own progress") {
    game_state state = started_game(fixture_hub_course());
    const course_world_collectible& lost_ball = state.hub->world.collectibles[1];
    state.player.position = lost_ball.position;
    save_data online = state.online.progress;
    online.collected_ids.push_back(lost_ball.id);
    receive_online_progress(state, online);

    CHECK(&active_progress(state) == &state.save);
    CHECK(nearby_collectible(state).has_value());
    state.play = play_mode::online;
    CHECK(&active_progress(state) == &state.online.progress);
    CHECK(!nearby_collectible(state).has_value());
}

TEST_CASE("the first online snapshot sets the starting xp without drops") {
    game_state state = in_mode(started_hole(), play_mode::online);
    save_data progress;
    add_skill_xp(progress.skills, "fitness", 5000);

    receive_online_progress(state, progress);

    CHECK(skill_xp(state.online.progress.skills, "fitness") == 5000);
    CHECK(state.xp_drops.empty());
    CHECK(state.pending_xp_drop_amounts["fitness"] == 0);
}

TEST_CASE("online progress from the server shows gained xp as drops and leaves the save alone") {
    game_state state = in_mode(started_hole(), play_mode::online);
    const std::string offline_save = json_of(state.save);
    const int threshold = state.tuning.xp_drops.min_visible_xp;
    receive_online_progress(state, save_data{});

    save_data progress = state.online.progress;
    add_skill_xp(progress.skills, "fitness", threshold + 3);
    receive_online_progress(state, progress);

    CHECK(skill_xp(state.online.progress.skills, "fitness") == threshold + 3);
    REQUIRE(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].skill_id == "fitness");
    CHECK(state.xp_drops[0].xp == threshold + 3);
    CHECK(json_of(state.save) == offline_save);

    receive_online_progress(state, progress);
    CHECK(state.xp_drops.size() == 1U);
    CHECK(state.xp_drops[0].xp == threshold + 3);
}

TEST_CASE("turning while aiming is sent as motion") {
    game_state state = started_hole();
    state.play = play_mode::online;
    enter_aiming(state);
    for (int i = 0; i < 10; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    const std::size_t before = state.net_commands.size();

    game_input left;
    left.turn_left_held = true;
    update_game(state, left, 0.05f);

    REQUIRE(state.net_commands.size() == before + 1);
    CHECK(state.net_commands.back().motion.mode == motion_mode::aim);
    CHECK(state.net_commands.back().motion.turn_rate == state.tuning.player.aim_turn_rate);
}

TEST_CASE("offline play never sends motion") {
    game_state state = started_hole();
    for (int i = 0; i < 20; ++i) {
        update_game(state, walk_forward(), 0.05f);
    }
    CHECK(state.net_commands.empty());
}

TEST_CASE("online motion is sent on intent changes, not every frame") {
    game_state state = started_hole();
    state.play = play_mode::online;

    update_game(state, game_input{}, 0.016f);
    REQUIRE(command_types(state) == std::vector<net_command_type>{net_command_type::motion});
    CHECK(state.net_commands[0].motion.mode == motion_mode::idle);
    CHECK(state.net_commands[0].motion.zone == 0);

    for (int i = 0; i < 60; ++i) {
        update_game(state, game_input{}, 0.05f);
    }
    CHECK(state.net_commands.size() == 1U);

    update_game(state, walk_forward(), 0.016f);
    REQUIRE(state.net_commands.size() == 2U);
    const net_motion walking = state.net_commands[1].motion;
    CHECK(walking.mode == motion_mode::walk);
    CHECK(walking.speed == state.tuning.player.walk_speed);
    CHECK(walking.turn_rate == 0.0f);

    // Walking straight on: only the heartbeat, once 2 s have passed.
    for (int i = 0; i < 38; ++i) {
        update_game(state, walk_forward(), 0.05f);
    }
    CHECK(state.net_commands.size() == 2U);
    for (int i = 0; i < 3; ++i) {
        update_game(state, walk_forward(), 0.05f);
    }
    CHECK(state.net_commands.size() == 3U);
}

TEST_CASE("online motion never goes faster than the minimum interval") {
    game_state state = started_hole();
    state.play = play_mode::online;
    game_input left;
    left.turn_left_held = true;
    game_input right;
    right.turn_right_held = true;

    // Four seconds of turning back and forth every frame.
    for (int i = 0; i < 240; ++i) {
        update_game(state, i % 2 == 0 ? left : right, 1.0f / 60.0f);
    }

    const float max_per_second = 1.0f / state.tuning.net.motion_min_interval_seconds;
    CHECK(static_cast<float>(state.net_commands.size()) <= 4.0f * max_per_second + 1.0f);
    CHECK(state.net_commands.size() > 4U);
}

TEST_CASE("motion is corrected when extrapolation drifts too far") {
    const net_tuning tuning = test_net_tuning();
    net_motion sent;
    sent.mode = motion_mode::cart;
    sent.speed = 5.0f;
    const motion_sync_state sync{sent, 1.0f};

    net_motion on_track = sent;
    on_track.position = extrapolate_motion(sent, 1.0f) + glm::vec3(0.5f, 0.0f, 0.0f);
    CHECK(!should_send_motion(sync, on_track, tuning));

    net_motion drifted = on_track;
    drifted.position = extrapolate_motion(sent, 1.0f) + glm::vec3(1.5f, 0.0f, 0.0f);
    CHECK(should_send_motion(sync, drifted, tuning));

    const motion_sync_state just_sent{sent, 0.1f};
    CHECK(!should_send_motion(just_sent, drifted, tuning));
    CHECK(should_send_motion(motion_sync_state{}, sent, tuning));
}

TEST_CASE("extrapolated motion follows the yaw convention, straight and turning") {
    net_motion motion;
    motion.position = glm::vec3(1.0f, 2.0f, 3.0f);
    motion.yaw = 0.0f;
    motion.speed = 4.0f;
    CHECK(near(extrapolate_motion(motion, 2.0f), glm::vec3(1.0f, 2.0f, 11.0f), 0.0001f));

    // A quarter circle turning towards +X from facing +Z ends facing +X, one
    // radius along each axis.
    motion.turn_rate = glm::radians(90.0f);
    const float radius = motion.speed / motion.turn_rate;
    CHECK(near(extrapolate_motion(motion, 1.0f), glm::vec3(1.0f + radius, 2.0f, 3.0f + radius), 0.0001f));

    // Turning matches stepping the same rules the player moves by.
    glm::vec3 stepped = motion.position;
    float yaw = motion.yaw;
    const int steps = 10000;
    for (int i = 0; i < steps; ++i) {
        const float dt = 0.7f / steps;
        yaw += motion.turn_rate * dt;
        stepped += yaw_direction(yaw) * motion.speed * dt;
    }
    CHECK(near(extrapolate_motion(motion, 0.7f), stepped, 0.01f));
}

TEST_CASE("online, the server hears where the player stands right before entering a hole") {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    const glm::vec3 start = state.hub->markers[1].start_position;
    state.player.position = start;
    state.net_commands.clear();
    update_game(state, action_input(), 0.016f);

    REQUIRE(state.net_commands.size() >= 2U);
    CHECK(state.net_commands[0].type == net_command_type::motion);
    CHECK(state.net_commands[0].motion.zone == hub_zone);
    CHECK(near(horizontal(state.net_commands[0].motion.position), horizontal(start), 0.01f));
    CHECK(state.net_commands[1].type == net_command_type::enter_hole);
}

TEST_CASE("online motion pauses while a shot plays") {
    game_state state = started_hole();
    state.play = play_mode::online;
    hit_selected_club(state);
    REQUIRE(state.shot.has_value());
    state.net_commands.clear();
    for (int i = 0; i < 40; ++i) {
        update_game(state, game_input{}, 0.05f);
        if (!state.shot) {
            break;  // once it has played, motion may go again
        }
        for (const net_command& command : state.net_commands) {
            CHECK(command.type != net_command_type::motion);
        }
        state.net_commands.clear();
    }
}

TEST_CASE("no emotes while a shot plays, which may hole and end them") {
    game_state state = started_hole();
    state.play = play_mode::online;
    hit_selected_club(state);
    REQUIRE(state.shot.has_value());
    state.net_commands.clear();
    game_input smoke;
    smoke.smoke = true;
    update_game(state, smoke, 0.016f);
    CHECK(!state.smoke_emote.active);
    CHECK(gameplay_commands(state).empty());
}

TEST_CASE("a cart getting onto a road is an intent change") {
    const net_tuning tuning = test_net_tuning();
    net_motion off_road;
    off_road.mode = motion_mode::cart;
    off_road.speed = 10.0f;
    net_motion on_road = off_road;
    on_road.on_road = true;
    on_road.position = extrapolate_motion(off_road, 0.5f);
    CHECK(should_send_motion(motion_sync_state{off_road, 0.5f}, on_road, tuning));
    net_motion still_on_road = on_road;
    still_on_road.position = extrapolate_motion(on_road, 0.5f);
    CHECK(!should_send_motion(motion_sync_state{on_road, 0.5f}, still_on_road, tuning));
}
