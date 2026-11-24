#include "doctest.h"

#include "game/ball_hit_rules.h"
#include "game/effect_catalog.h"
#include "game/shot_simulation.h"
#include "game/shot_snapshot.h"
#include "game/vignette_strength.h"
#include "physics/vector_math.h"
#include "renderer/vignette_overlay.h"

#include "hit_effect_rules.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>

namespace {
const ball_hit_tuning& hits() {
    return shipped_content().tuning.ball_hits;
}

constexpr float person_m = 1.8f;
constexpr float drive_mps = 60.0f;

int knockouts(const float speed, const float height, const bool self_hit, const int rolls) {
    int count = 0;
    for (int seed = 0; seed < rolls; ++seed) {
        count += hit_outcome(speed, height, person_m, self_hit, hits(), static_cast<std::uint64_t>(seed) * 7919u + 3u, 0) ==
                         hit_outcome_kind::knocked_out
            ? 1
            : 0;
    }
    return count;
}

// A ball at `height` over a standing person's feet, `speed` m/s flat towards them from 10 m away.
shot_result drive_into_person(game_state& state, const float height, const float speed) {
    still_air(state);
    const glm::vec3 feet = resting_on_terrain(state, state.ball->position + glm::vec3(10.0f, 0.0f, 0.0f)) -
        glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f);
    shot_course course = current_shot_course(state);
    snapshot_entry person;
    person.position = feet;
    const std::vector<solid> solids = snapshot_solids({person}, state.tuning);
    course.dynamic.insert(course.dynamic.end(), solids.begin(), solids.end());
    ball_state ball = still_ball(state);
    ball.position = feet + glm::vec3(-10.0f, height, 0.0f);
    ball.velocity = glm::vec3(speed / state.tuning.scale.meters_per_world_unit, 0.0f, 0.0f);
    return simulate_ball(ball, course, state.tuning);
}
}

TEST_CASE("ball hit bands follow the contact height against the person's height") {
    CHECK(contact_band(0.3f, person_m, hits()) == body_band::legs);
    CHECK(contact_band(1.0f, person_m, hits()) == body_band::body);
    CHECK(contact_band(1.7f, person_m, hits()) == body_band::head);
}

TEST_CASE("a ball hitting a person: nothing at a putt's speed, a daze lower down at a drive's, never a knockout") {
    CHECK(hit_outcome(3.0f, 1.7f, person_m, false, hits(), 1, 0) == hit_outcome_kind::none);
    CHECK(hit_outcome(hits().min_speed_mps - 0.1f, 1.7f, person_m, false, hits(), 1, 0) == hit_outcome_kind::none);
    CHECK(hit_outcome(hits().min_speed_mps + 0.1f, 0.5f, person_m, false, hits(), 1, 0) == hit_outcome_kind::dazed);
    CHECK(hit_speed_share(hits().putt_speed_mps, hits()) == 0.0f);
    CHECK(hit_speed_share(hits().drive_speed_mps + 10.0f, hits()) == 1.0f);
    for (const float height : {0.2f, 0.9f}) {
        CHECK(knockouts(drive_mps, height, false, 2000) == 0);
        CHECK(hit_outcome(drive_mps, height, person_m, false, hits(), 5, 0) == hit_outcome_kind::dazed);
    }
}

TEST_CASE("a head hit at a drive's speed knocks out about one time in three, a slower one less, the shooter's own never") {
    constexpr int rolls = 30000;
    const float full = static_cast<float>(knockouts(drive_mps, 1.7f, false, rolls)) / static_cast<float>(rolls);
    CHECK(std::abs(full - hits().head_knockout_max) < 0.015f);
    const float half_speed = hits().putt_speed_mps + 0.5f * (hits().drive_speed_mps - hits().putt_speed_mps);
    const float half = static_cast<float>(knockouts(half_speed, 1.7f, false, rolls)) / static_cast<float>(rolls);
    CHECK(std::abs(half - 0.5f * hits().head_knockout_max) < 0.015f);
    CHECK(knockouts(drive_mps, 1.7f, true, rolls) == 0);
    CHECK(hit_outcome(drive_mps, 1.7f, person_m, true, hits(), 1, 0) == hit_outcome_kind::dazed);
}

TEST_CASE("the same seed and hit give the same outcome, and each hit of a shot rolls apart") {
    int differing = 0;
    for (int seed = 0; seed < 500; ++seed) {
        const std::uint64_t s = static_cast<std::uint64_t>(seed);
        const hit_outcome_kind first = hit_outcome(drive_mps, 1.7f, person_m, false, hits(), s, 0);
        CHECK(first == hit_outcome(drive_mps, 1.7f, person_m, false, hits(), s, 0));
        differing += first != hit_outcome(drive_mps, 1.7f, person_m, false, hits(), s, 1) ? 1 : 0;
    }
    CHECK(differing > 0);
}

TEST_CASE("snapshot entries map to their solids with the shooter last") {
    const std::vector<snapshot_entry> entries{
        snapshot_entry{snapshot_kind::person, glm::vec3(0.0f), 0.0f, true},
        snapshot_entry{snapshot_kind::cart, glm::vec3(5.0f, 0.0f, 0.0f), 0.0f, false},
        snapshot_entry{snapshot_kind::ball, glm::vec3(9.0f, 0.02f, 0.0f), 0.0f, false}};
    CHECK(snapshot_entry_of_solid(entries, 0) == 1U);
    CHECK(snapshot_entry_of_solid(entries, 1) == 2U);
    CHECK(snapshot_entry_of_solid(entries, 2) == 0U);
    CHECK(!snapshot_entry_of_solid(entries, 3));
}

TEST_CASE("a ball into a standing person records one hit with its speed and contact height in metres") {
    game_state state = started_hole();
    const shot_result body = drive_into_person(state, 1.0f, drive_mps);
    REQUIRE(!body.person_hits.empty());
    const person_hit& first = body.person_hits.front();
    CHECK(first.speed_mps > 45.0f);
    CHECK(first.speed_mps < drive_mps + 1.0f);
    CHECK(first.contact_height_m > 0.6f);
    CHECK(first.contact_height_m < 1.1f);
    CHECK(first.time > 0.0f);
    CHECK(first.time < 0.5f);
    const int person_events = static_cast<int>(std::count_if(body.events.begin(), body.events.end(), [](const shot_event& event) {
        return event.kind == shot_event_kind::person_hit;
    }));
    CHECK(static_cast<int>(body.person_hits.size()) == person_events);

    const shot_result head = drive_into_person(state, 1.8f, drive_mps);
    REQUIRE(!head.person_hits.empty());
    CHECK(contact_band(head.person_hits.front().contact_height_m, person_m, hits()) == body_band::head);
}

TEST_CASE("a struck shot's hit names the person's snapshot entry, the shooter's listed first or not") {
    game_state state = started_hole();
    still_air(state);
    const glm::vec3 ball = state.ball->position;
    const glm::vec3 ahead = ball + yaw_direction(state.player.yaw) * 3.0f;
    const club_definition& putter =
        *std::find_if(state.clubs.begin(), state.clubs.end(), [](const club_definition& club) { return club.label == "P"; });
    shot_setup putt = stock_swing(putter, state.tuning, 0.6f, lie_at(state.area, ball), yaw_towards(ball, ahead));
    putt.ball_start = ball;
    golfer_profile steady;
    steady.spread_scale = 0.0f;
    const snapshot_entry other{snapshot_kind::person, resting_on_terrain(state, ahead) -
                                   glm::vec3(0.0f, state.tuning.scale.ball_physics_radius_meters, 0.0f), 0.0f, false, 91};
    const snapshot_entry me{snapshot_kind::person, ball, 0.0f, true, 17};
    const snapshot_entry cart{snapshot_kind::cart, ball + glm::vec3(0.0f, 0.0f, 40.0f), 0.0f, false};
    for (const std::vector<snapshot_entry>& snapshot : {std::vector<snapshot_entry>{me, other, cart},
                                                         std::vector<snapshot_entry>{cart, other, me},
                                                         std::vector<snapshot_entry>{other, me}}) {
        const std::size_t other_at = static_cast<std::size_t>(
            std::find_if(snapshot.begin(), snapshot.end(), [](const snapshot_entry& entry) { return entry.account == 91; }) -
            snapshot.begin());
        const shot_result result = simulate_struck_shot(shot_record{putt, 7, snapshot}, steady, current_shot_course(state),
                                                        state.tuning, state.clubs);
        REQUIRE(!result.person_hits.empty());
        for (const person_hit& hit : result.person_hits) {
            CHECK(hit.entry == other_at);
            CHECK(hit.speed_mps < 8.0f);
        }
        // A putt's speed does nothing to anyone.
        CHECK(hit_consequences(result, snapshot, state.tuning, 7).empty());
    }
}

TEST_CASE("the same shot dazes the same account on the server and offline") {
    const game_content& content = shipped_content();
    const event_effect_definition* dazed = content.effects.find(dazed_effect_id);
    REQUIRE(dazed != nullptr);
    shot_result result;
    result.person_hits = {person_hit{1, 0.4f, drive_mps, 0.9f}, person_hit{0, 0.9f, drive_mps, 1.7f}, person_hit{2, 1.0f, drive_mps, 0.9f}};
    const std::vector<snapshot_entry> online{snapshot_entry{snapshot_kind::person, glm::vec3(0.0f), 0.0f, true, 17},
                                             snapshot_entry{snapshot_kind::person, glm::vec3(1.0f), 0.0f, false, 91},
                                             snapshot_entry{snapshot_kind::cart, glm::vec3(2.0f), 0.0f, false}};
    std::vector<snapshot_entry> offline = online;
    for (snapshot_entry& entry : offline) {
        entry.account = 0;
    }
    const std::vector<hit_consequence> server = hit_consequences(result, online, content.tuning, 12345);
    const std::vector<hit_consequence> local = hit_consequences(result, offline, content.tuning, 12345);
    REQUIRE(server.size() == 2U);  // the cart is no one; me: dazed, never knocked out
    REQUIRE(local.size() == server.size());
    CHECK(server[0].account == 91U);
    CHECK(server[1].account == 17U);
    CHECK(server[1].outcome == hit_outcome_kind::dazed);
    for (std::size_t i = 0; i < server.size(); ++i) {
        CHECK(local[i].entry == server[i].entry);
        CHECK(local[i].outcome == server[i].outcome);
        CHECK(local[i].time == server[i].time);
    }

    // The effect rows the server writes for the first hit are the effects offline starts.
    const std::int64_t now = 1000000;
    const std::vector<active_effect> started = effects_after_hits({}, {local[0]}, *dazed, now);
    const hit_effect_change change = effects_started_by_hit({}, *dazed, now + 400);
    REQUIRE(started.size() == change.add.size());
    for (std::size_t i = 0; i < started.size(); ++i) {
        CHECK(started[i].started_at_ms == change.add[i].started_at_ms);
        CHECK(started[i].ends_at_ms == change.add[i].ends_at_ms);
        CHECK(started[i].effect.target == change.add[i].effect.target);
    }
    CHECK(started.front().started_at_ms == now + 400);
}

TEST_CASE("a hit refreshes the dazed rows and leaves other sources alone") {
    const event_effect_definition* dazed = shipped_content().effects.find(dazed_effect_id);
    REQUIRE(dazed != nullptr);
    const std::vector<active_effect> earlier = effects_after_event({}, *dazed, 1000);
    active_effect beer;
    beer.source = item_source("beer");
    beer.effect = earlier.front().effect;
    beer.ends_at_ms = 90000;
    std::vector<running_effect_ref> rows{running_effect_ref{4, beer}};
    for (std::size_t i = 0; i < earlier.size(); ++i) {
        rows.push_back(running_effect_ref{10 + i, earlier[i]});
    }
    const hit_effect_change change = effects_started_by_hit(rows, *dazed, 5000);
    // Dazed at 1 s, hit again at 5 s: the old rows end at 5 s and stay.
    CHECK(change.remove_ids.empty());
    CHECK(change.end_at.size() == earlier.size());
    for (const auto& [id, ends] : change.end_at) {
        CHECK(id != 4U);
        CHECK(ends == 5000);
    }
    REQUIRE(change.add.size() == dazed->effects.size());
    CHECK(change.add.front().started_at_ms == 5000);
    CHECK(effect_running(change.add.front(), 5001));
    CHECK(!effect_running(change.add.front(), 4999));
}

TEST_CASE("a second hit while dazed leaves the ball's travel time dazed and drops rows not yet begun") {
    const event_effect_definition* dazed = shipped_content().effects.find(dazed_effect_id);
    REQUIRE(dazed != nullptr);
    const std::vector<active_effect> first = effects_after_event({}, *dazed, 1000);
    std::vector<running_effect_ref> rows;
    for (std::size_t i = 0; i < first.size(); ++i) {
        rows.push_back(running_effect_ref{1 + i, first[i]});
    }
    const hit_effect_change change = effects_started_by_hit(rows, *dazed, 3000);
    // Applying it: every row still running before the new start keeps running to it.
    std::vector<active_effect> after;
    for (const running_effect_ref& row : rows) {
        active_effect each = row.effect;
        for (const auto& [id, ends] : change.end_at) {
            if (id == row.id) { each.ends_at_ms = ends; }
        }
        after.push_back(each);
    }
    after.insert(after.end(), change.add.begin(), change.add.end());
    for (const std::int64_t at : {1000, 2999, 3000, 10000}) {
        CHECK(effective_value(5.0f, effect_target_walk_speed, after, at) < 5.0f);
    }
    CHECK(effective_value(5.0f, effect_target_walk_speed, after, 500) == 5.0f);
    // A hit that lands before a later one already written replaces it.
    const hit_effect_change earlier = effects_started_by_hit({running_effect_ref{9, first.front()}}, *dazed, 900);
    CHECK(earlier.remove_ids == std::vector<std::uint64_t>{9});
}

TEST_CASE("a vignette is full where its effect starts and fades evenly to nothing") {
    CHECK(vignette_strength(1000, 1000, 2.0f) == 1.0f);
    CHECK(near(vignette_strength(1000, 2000, 2.0f), 0.5f));
    CHECK(vignette_strength(1000, 3000, 2.0f) == 0.0f);
    CHECK(vignette_strength(1000, 9000, 2.0f) == 0.0f);
    CHECK(vignette_strength(1000, 500, 2.0f) == 0.0f);
    float last = 2.0f;
    for (std::int64_t now = 1000; now <= 3200; now += 50) {
        const float strength = vignette_strength(1000, now, 2.0f);
        CHECK(strength <= last);
        last = strength;
    }

    const game_content& content = shipped_content();
    const event_effect_definition* dazed = content.effects.find(dazed_effect_id);
    REQUIRE(dazed != nullptr);
    REQUIRE(dazed->vignette);
    const std::vector<active_effect> running = effects_after_event({}, *dazed, 10000);
    CHECK(running_vignettes(running, content.effects, 9999).empty());
    const std::vector<vignette_look> at_start = running_vignettes(running, content.effects, 10000);
    REQUIRE(at_start.size() == 1U);
    CHECK(at_start[0].strength == 1.0f);
    CHECK(at_start[0].colour == dazed->vignette->colour);
    CHECK(running_vignettes(running, content.effects, 10000 + effect_milliseconds(dazed->vignette->seconds)).empty());
}

TEST_CASE("the vignette draws the colour at the screen's edge and nothing in the middle") {
    overlay_batch batch;
    draw_vignette(batch, glm::vec3(0.8f, 0.0f, 0.0f), 0.0f);
    CHECK(batch.vertices.empty());
    draw_vignette(batch, glm::vec3(0.8f, 0.0f, 0.0f), 1.0f);
    REQUIRE(batch.vertices.size() == 4 * overlay_vertices_per_quad);
    for (const overlay_vertex& vertex : batch.vertices) {
        const float edge = std::max(std::abs(vertex.position.x), std::abs(vertex.position.y));
        if (edge >= 1.0f) {
            CHECK(near(vertex.color.a, vignette_edge_alpha));
        } else {
            CHECK(near(edge, 1.0f - vignette_inset));
            CHECK(vertex.color.a == 0.0f);
        }
        CHECK(vertex.color.r == 0.8f);
    }
}
