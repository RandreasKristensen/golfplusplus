#include "doctest.h"

#include "core/render_frame.h"
#include "game/group_round.h"
#include "game/online_play.h"
#include "game/scorecard.h"
#include "game/text_ids.h"
#include "play_rules.h"
#include "renderer/scorecard_overlay.h"

#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {
game_state grouped_game(const int members = 2) {
    game_state state = started_game(fixture_hub_course());
    state.play = play_mode::online;
    state.online.account_id = 1;
    state.online.display_name = "ME";
    for (int account = 1; account <= members; ++account) {
        room_player player;
        player.name = account == 1 ? "ME" : "OTHER " + std::to_string(account);
        player.group_id = 7;
        player.round_started = true;
        player.round_strokes.assign(state.course_holes.size(), 0);
        state.online.players[static_cast<std::uint64_t>(account)] = player;
    }
    state.scorecard_active = true;
    return state;
}

scorecard_data frame_card(const game_state& state) {
    const render_data data = make_render_data(state, shipped_text_assets(), shipped_content().skills,
                                               render_meshes{}, test_props(), 60.0f, true);
    CHECK(data.show_scorecard || data.show_course_results);
    return data.scorecard;
}

std::vector<std::string> labels_of(const scorecard_data& card, const scorecard_label_kind kind, const bool results = false) {
    std::vector<std::string> values;
    for (const scorecard_label& label : scorecard_labels(shipped_text_assets(), card, results)) {
        if (label.kind == kind) {
            values.push_back(label.text);
        }
    }
    return values;
}
}

TEST_CASE("scorecard: a fresh four-player group ignores holed shot history") {
    game_state state = grouped_game(4);
    const std::vector<int> blank(state.course_holes.size(), 0);
    for (const auto& [account, player] : state.online.players) {
        (void)player;
        for (std::size_t hole = 0; hole < state.course_holes.size(); ++hole) {
            shot_setup setup;
            setup.club_id = state.clubs.back().id;
            setup.ball_start = state.holes[hole].pin_position;
            state.online.shots.push_back(played_shot(account, static_cast<int>(hole), static_cast<int>(account) + 2,
                                                     setup, setup.ball_start, true));
        }
    }
    watch_room(state, 0.0f);
    const scorecard_data card = frame_card(state);
    REQUIRE(card.players.size() == 4U);
    for (const scorecard_player& player : card.players) {
        CHECK(!player.name.empty());
        CHECK(player.strokes == blank);
        CHECK(player.total_strokes == 0);
        CHECK(!player.current_hole);
    }
    const std::vector<std::string> scores = labels_of(card, scorecard_label_kind::strokes);
    CHECK(scores.size() == 4U * blank.size());
    CHECK(std::all_of(scores.begin(), scores.end(), [](const std::string& score) { return score.empty(); }));
}

TEST_CASE("scorecard: joining preserves my name and current hole without completing it") {
    game_state state = grouped_game();
    state.online.players[1].group_id = 0;
    state.round = complete_hole(*state.round, 0, 4);
    REQUIRE(tee_up(state, 1, false));
    state.ball->strokes = 2;
    const scorecard_data solo = frame_card(state);
    REQUIRE(solo.players[0].current_hole);
    CHECK(solo.players[0].current_hole->strokes == 2);

    // The account's name is known before the room's joined player row arrives.
    state.online.players[1].name.clear();
    state.online.players[1].group_id = 7;
    state.online.balls[2] = room_ball{2, state.holes[2].tee_position, 0, false};
    scorecard_data card = frame_card(state);
    REQUIRE(card.players.size() == 2U);
    CHECK(card.players[0].name == "ME");
    CHECK(card.players[0].me);
    CHECK(card.players[0].strokes == solo.players[0].strokes);
    CHECK(card.players[0].total_strokes == 4);
    CHECK(card.players[0].relative_label == solo.players[0].relative_label);
    REQUIRE(card.players[0].current_hole);
    CHECK(card.players[0].current_hole->index == 1U);
    CHECK(card.players[0].current_hole->strokes == 2);
    REQUIRE(card.players[1].current_hole);
    CHECK(card.players[1].current_hole->index == 2U);
    CHECK(card.players[1].current_hole->strokes == 0);
    CHECK((labels_of(card, scorecard_label_kind::strokes) == std::vector<std::string>{"4", "2", "", "", "", "0"}));

    complete_current_hole(state);
    state.online.balls.erase(2);  // gave up the unfinished hole
    card = frame_card(state);
    CHECK(!card.players[0].current_hole);
    CHECK(!card.players[1].current_hole);
    CHECK(card.players[0].total_strokes == 6);
    CHECK((labels_of(card, scorecard_label_kind::strokes) == std::vector<std::string>{"4", "2", "", "", "", ""}));
}

TEST_CASE("scorecard: a new member round replaces all previous scores and a rejoin has no history") {
    game_state state = grouped_game();
    state.online.players[2].round_strokes = {4, 0, 3};
    watch_room(state, 0.0f);
    CHECK(frame_card(state).players[1].total_strokes == 7);
    state.online.players[2].round_strokes = {0, 0, 0};
    state.online.balls[2] = room_ball{1, state.holes[1].tee_position, 2, false};
    watch_room(state, 0.0f);
    CHECK((frame_card(state).players[1].strokes == std::vector<int>{0, 0, 0}));
    state.online.players[2].round_strokes = {0, 5, 0};
    watch_room(state, 0.0f);
    CHECK((frame_card(state).players[1].strokes == std::vector<int>{0, 5, 0}));

    const room_player returning = state.online.players[2];
    state.online.players.erase(2);
    state.online.balls.erase(2);
    watch_room(state, 0.0f);
    CHECK(frame_card(state).players.size() == 1U);
    state.online.players[2] = returning;
    state.online.players[2].round_strokes = {0, 0, 0};
    watch_room(state, 0.0f);
    CHECK(frame_card(state).players[1].total_strokes == 0);
}

TEST_CASE("scorecard: the server starts a fresh round only at the next tee-up") {
    const game_state state = grouped_game();
    const std::size_t holes = state.course_holes.size();
    const std::vector<int> blank(holes, 0);
    CHECK(tee_round_strokes({}, holes) == blank);
    const std::vector<int> partial{0, 5, 3};
    CHECK(tee_round_strokes(partial, holes) == partial);  // arbitrary hole order
    CHECK((tee_round_strokes({4, 5, 3}, holes) == blank));
    CHECK((tee_round_strokes({4, 5}, holes) == std::vector<int>{4, 5, 0}));
}

TEST_CASE("scorecard: completed group results separate hole ten, names and scores") {
    game_state state = grouped_game(5);
    // Repeat a loaded fixture hole to exercise both blocks of an 18-hole card.
    const hole_data hole = state.course_holes.front();
    state.course_holes.assign(18, hole);
    state.round = start_round(18);
    for (std::size_t index = 0; index < 18; ++index) {
        state.round = complete_hole(*state.round, index, index == 9 ? 10 : 3);
    }
    for (auto& [account, player] : state.online.players) {
        (void)account;
        player.round_strokes.assign(18, 4);
        player.round_strokes[9] = 10;
    }
    const scorecard_data card = build_scorecard_data(state, shipped_text_assets().strings);
    REQUIRE(card.finished);
    const std::vector<std::string> numbers = labels_of(card, scorecard_label_kind::hole, true);
    REQUIRE(numbers.size() == 18U);
    for (std::size_t index = 0; index < numbers.size(); ++index) {
        CHECK(numbers[index] == std::to_string(index + 1));
    }
    const std::vector<std::string> names = labels_of(card, scorecard_label_kind::name, true);
    CHECK((names == std::vector<std::string>{"ME", "OTHER 2", "OTHER 3", "OTHER 4", "OTHER 5", "ME", "OTHER 2", "OTHER 3", "OTHER 4", "OTHER 5"}));
    const std::vector<std::string> headings = labels_of(card, scorecard_label_kind::heading, true);
    CHECK(std::count(headings.begin(), headings.end(), "HOLE") == 2);
    CHECK(std::count(headings.begin(), headings.end(), "TOT") == 1);
    const std::vector<std::string> scores = labels_of(card, scorecard_label_kind::strokes, true);
    CHECK(std::count(scores.begin(), scores.end(), "10") == 5);

    const std::vector<scorecard_label> labels = scorecard_labels(shipped_text_assets(), card, true);
    for (std::size_t i = 0; i < labels.size(); ++i) {
        for (std::size_t j = i + 1; j < labels.size(); ++j) {
            const ui_rect& a = labels[i].box;
            const ui_rect& b = labels[j].box;
            const float width = std::min(rect_right(a), rect_right(b)) - std::max(rect_left(a), rect_left(b));
            const float height = std::min(rect_top(a), rect_top(b)) - std::max(rect_bottom(a), rect_bottom(b));
            CHECK((width < 0.00001f || height < 0.00001f));
        }
    }
    for (const overlay_grid grid : {overlay_grid{640, 480}, overlay_grid{854, 480}, overlay_grid{1120, 480}}) {
        overlay_batch batch;
        batch.grid = grid;
        draw_course_results(batch, shipped_text_assets(), card);
        CHECK(batch.truncated_text_count == 0U);
    }

    // A late observer reads the whole final card without seeing any shots.
    game_state observer = grouped_game(4);
    observer.course_holes = state.course_holes;
    observer.round = start_round(18);
    observer.online.players = state.online.players;
    CHECK(build_scorecard_data(observer, shipped_text_assets().strings).players[1].total_strokes == 78);
    start_next_round(state);
    CHECK(member_round_strokes(state, 1) == card.players.front().strokes);
    state.online.players[2].round_strokes = tee_round_strokes(state.online.players[2].round_strokes, 18);
    CHECK(member_round_strokes(state, 2) == std::vector<int>(18, 0));
}
