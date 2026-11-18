#pragma once

// My balls out: the ball in play (game_state::ball, the server's ball row) and
// my spares, balls at rest where I put them (game/spare_ball.h). Every ball out
// is one running array capped at the place's rules.balls_out: a hub's 10, a
// course's 1. The same mechanism later serves a provisional ball (Rule 18.3) and
// two balls (Rule 20.1c(3)) on a course.
// The decisions below read the one surface and never move a player or touch
// progress; shared by offline play and the server. Dropping is never refused
// in the hub (the owner's decision: the hub is for fooling about); lifting is
// the hold of F (game/ball_lift.h), a tap at a spare plays it.
#include "game/ball_kind.h"
#include "game/place_definition.h"
#include "game/play_area.h"
#include "game/shot_snapshot.h"
#include "game/spare_ball.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Where a ball dropped from `requested` rests, on the one surface under it;
// from `accepted` (the server's word on where the golfer is) when `requested`
// is not a point within warmup_reach_meters of it. None where the place's
// rules allow no drops: a course's balls are scored.
std::optional<glm::vec3> practice_drop_position(const play_area& area, const place_rules& rules, const glm::vec3& accepted,
    const glm::vec3& requested, const game_tuning& tuning);
bool practice_pickup_in_reach(ball_kind kind, bool resting, bool on_foot, const glm::vec3& accepted,
    const glm::vec3& ball, const game_tuning& tuning);

// The spares kept when a new ball goes into play: `spares` plus `demoted` (the
// ball I was playing, with id 0, when it lay at rest), cut to leave the new ball
// in play its place within `balls_out`. The oldest go first; the ball in play is
// never among them. Oldest first.
std::vector<spare_ball> spares_after_new_ball(std::vector<spare_ball> spares, const std::optional<spare_ball>& demoted,
    int balls_out);

// The spares a ball struck from `struck_from` may meet, as snapshot entries
// (game/shot_snapshot.h): those at rest out of their cups, but not one lying where
// the struck ball does (a ball dropped twice on the spot), which it starts inside.
std::vector<snapshot_entry> spares_in_snapshot(const std::vector<spare_ball>& spares, const glm::vec3& struck_from,
    const game_tuning& tuning);

// The ball in play as the swap rule sees it.
struct ball_in_play_state {
    ball_kind kind = ball_kind::practice;
    bool resting = true;
};
// Whether `spare` may be played from `accepted`: in reach on foot, with no ball
// in play or a practice ball at rest to put down. The error id, or empty.
std::string play_spare_error(const std::optional<ball_in_play_state>& playing, bool on_foot, const glm::vec3& accepted,
    const spare_ball& spare, const game_tuning& tuning);

// The nearest of my balls to lift: `in_play` is where my ball in play lies when
// the rules let me lift it, else none. A spare wins a tie, the oldest first.
struct lift_choice {
    bool in_play = false;
    std::uint64_t spare_id = 0;
};
std::optional<lift_choice> nearest_ball_to_lift(const std::optional<glm::vec3>& in_play, const std::vector<spare_ball>& spares,
    bool on_foot, const glm::vec3& accepted, const game_tuning& tuning);

struct game_state;
struct my_ball;
// My spares, oldest first: offline game_state::spare_balls, online my practice_ball rows.
std::vector<spare_ball> my_spare_balls(const game_state& state);
// G: a new ball at my feet becomes my ball in play (online it waits for the
// server's rows). The ball I was playing joins the spares when it lay at rest.
void request_practice_drop(game_state& state);
// Offline: `fresh` becomes my ball in play, as a drop or a tee at a bay.
void put_new_ball_in_play(game_state& state, my_ball fresh);
// Hold F: lifts my practice ball in play, or the spare `id`; false when it may not be lifted.
bool pick_up_practice_ball(game_state& state);
bool pick_up_spare_ball(game_state& state, std::uint64_t id);
// Tap F at a spare: it becomes my ball in play and the ball I was playing, at rest, a spare.
bool play_spare_ball(game_state& state, std::uint64_t id);
