#include "doctest.h"

#include "game/mode_dispatch.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "physics/vector_math.h"

#include "test_support.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/constants.hpp>

namespace {
bool has_event(const shot_result& result, const shot_event_kind kind) {
    return std::any_of(result.events.begin(), result.events.end(), [kind](const shot_event& event) { return event.kind == kind; });
}

const club_definition& club_labelled(const game_state& state, const std::string& label) {
    return *std::find_if(state.clubs.begin(), state.clubs.end(), [&label](const club_definition& club) { return club.label == label; });
}

// The setup of `label` at `ball`, aimed at `target`, swung back `backswing`, stock otherwise.
shot_setup aimed(const game_state& state, const std::string& label, const glm::vec3& ball, const glm::vec3& target,
                 const float backswing) {
    shot_setup setup = stock_swing(club_labelled(state, label), state.tuning, backswing, lie_at(state.area, ball),
                                   yaw_towards(ball, target));
    setup.ball_start = ball;
    return setup;
}

// A golfer whose swing never strays: the shot goes where it is set up.
golfer_profile steady() {
    golfer_profile golfer;
    golfer.spread_scale = 0.0f;
    return golfer;
}

shot_result struck(const game_state& state, const shot_setup& setup, const std::vector<snapshot_entry>& snapshot) {
    return simulate_struck_shot(shot_record{setup, 7, snapshot}, steady(), current_shot_course(state), state.tuning,
                                state.clubs);
}

// Along the tee-to-pin line of my ball's hole: the pin, and the green's
// edge walking back from it.
struct green_line {
    glm::vec3 pin{0.0f};
    glm::vec3 back{0.0f};  // a unit step from the pin towards the tee
    float edge = 0.0f;     // metres from the pin to the green's edge
};

green_line line_to_pin(const game_state& state) {
    green_line line;
    line.pin = current_shot_course(state).hole.pin;
    line.back = safe_normalize(horizontal(state.ball->position - line.pin), glm::vec3(1.0f, 0.0f, 0.0f));
    while (lie_at(state.area, line.pin + line.back * (line.edge + 0.25f)) == ball_lie::green && line.edge < 60.0f) {
        line.edge += 0.25f;
    }
    return line;
}

glm::vec3 on_ground(const game_state& state, const glm::vec3& point) {
    return resting_on_terrain(state, point);
}
}

TEST_CASE("snapshot solids: people, carts and balls stop the ball, the shooter last") {
    const game_tuning& tuning = shipped_content().tuning;
    const std::vector<solid> solids = snapshot_solids(
        {snapshot_entry{snapshot_kind::person, glm::vec3(0.0f), 0.0f, true}, snapshot_entry{snapshot_kind::cart, glm::vec3(5.0f, 0.0f, 0.0f), 0.0f, false},
         snapshot_entry{snapshot_kind::ball, glm::vec3(9.0f, 0.02f, 0.0f), 0.0f, false}},
        tuning);
    REQUIRE(solids.size() == 3U);
    CHECK(solids[0].kind == solid_kind::cart);
    CHECK(solids[1].kind == solid_kind::ball);
    CHECK(solids[2].kind == solid_kind::person);
    for (const solid& thing : solids) {
        CHECK(thing.stops_ball);
    }
    CHECK(near(solids[2].height, tuning.rangefinder.person_height));
}

TEST_CASE("a player standing in the way is hit, and the ball is played where it lies") {
    game_state state = started_hole();
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 ahead = ball + yaw_direction(state.player.yaw) * 3.0f;
    const shot_setup putt = aimed(state, "P", ball, ahead, 0.6f);
    const shot_result clear = struck(state, putt, {});
    REQUIRE(horizontal_distance(clear.rest_position, ball) > 4.0f);

    const glm::vec3 standing = on_ground(state, ahead);
    const shot_result blocked = struck(state, putt, {snapshot_entry{snapshot_kind::person, standing, 0.0f, false}});
    CHECK(has_event(blocked, shot_event_kind::person_hit));
    CHECK(blocked.penalty_strokes == 0);
    CHECK(horizontal_distance(blocked.rest_position, ball) < 3.0f);
}

TEST_CASE("the one who hit it is never struck by the club's own launch, but a ricochet off a post comes back and hits them") {
    game_state state = started_hole();
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 forward = yaw_direction(state.player.yaw);
    // Standing right over the ball: inside the grace it passes through.
    const shot_setup putt = aimed(state, "P", ball, ball + forward * 5.0f, 0.5f);
    const shot_result through = struck(state, putt, {snapshot_entry{snapshot_kind::person, on_ground(state, ball), 0.0f, true}});
    CHECK(!has_event(through, shot_event_kind::person_hit));

    // A post just ahead sends it straight back, past the grace, into me behind the ball.
    shot_course course = current_shot_course(state);
    solid post;
    post.kind = solid_kind::pole;
    post.base = on_ground(state, ball + forward * 1.5f) - glm::vec3(0.0f, 0.5f, 0.0f);
    post.radius = 0.15f;
    post.height = 3.0f;
    post.stops_ball = true;
    post.restitution = 0.8f;
    course.dynamic.insert(course.dynamic.begin(), post);
    const glm::vec3 behind = on_ground(state, ball - forward * 1.0f);
    const shot_result ricochet = simulate_struck_shot(
        shot_record{aimed(state, "P", ball, ball + forward * 5.0f, 1.0f), 7, {snapshot_entry{snapshot_kind::person, behind, 0.0f, true}}},
        steady(), course, state.tuning, state.clubs);
    CHECK(has_event(ricochet, shot_event_kind::pole_hit));
    CHECK(has_event(ricochet, shot_event_kind::person_hit));
}

TEST_CASE("a putt hitting another ball at rest on the green costs two strokes and leaves it there; a shot from off the green costs nothing") {
    game_state state = started_hole();
    const green_line line = line_to_pin(state);
    REQUIRE(line.edge > 4.0f);
    const glm::vec3 other = on_ground(state, line.pin + line.back * 1.5f);
    const std::vector<snapshot_entry> resting{snapshot_entry{snapshot_kind::ball, other, 0.0f, false}};

    const glm::vec3 on_green = on_ground(state, line.pin + line.back * 4.0f);
    REQUIRE(lie_at(state.area, on_green) == ball_lie::green);
    const shot_setup putt = aimed(state, "P", on_green, other, 0.6f);
    const shot_result putted = struck(state, putt, resting);
    CHECK(has_event(putted, shot_event_kind::ball_hit));
    CHECK(putted.penalty_strokes == 2);
    CHECK(!putted.lost);

    const glm::vec3 off_green = on_ground(state, line.pin + line.back * (line.edge + 1.5f));
    REQUIRE(lie_at(state.area, off_green) != ball_lie::green);
    const shot_setup chip = aimed(state, "P", off_green, other, 0.9f);
    const shot_result chipped = struck(state, chip, resting);
    CHECK(has_event(chipped, shot_event_kind::ball_hit));
    CHECK(chipped.penalty_strokes == 0);

    // Played offline, the strokes count: the putt was 1, the next is played as 4.
    state.ball->strokes = 1;
    play_shot(state, putted);
    for (int i = 0; i < 2000 && state.shot; ++i) {
        update_game(state, game_input{}, 0.02f);
    }
    CHECK(state.ball->strokes == 3);
}

TEST_CASE("offline my shot meets my cart where it stands") {
    game_state state = started_hole();
    while (state.clubs[state.selected_club].label != "P") {
        state.selected_club += 1;
    }
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 forward = yaw_direction(state.player.yaw);
    state.cart.pose.position = on_ground(state, ball + forward * 4.0f);
    state.cart.pose.yaw = state.player.yaw + glm::half_pi<float>();
    enter_addressing(state);
    REQUIRE(state.mode == game_mode::addressing);
    state.address.setup.aim_angle = state.player.yaw;
    state.address.setup.face_open = 0.0f;
    state.address.setup.backswing = 0.8f;
    update_game(state, swing_input(), 0.016f);
    swing_through(state);
    REQUIRE(state.shot.has_value());
    CHECK(has_event(state.shot->result, shot_event_kind::board_hit));
}
