#include "doctest.h"

#include "core/effect_shelf_frame.h"
#include "game/effect_shelf.h"
#include "game/effect_text.h"
#include "game/mode_dispatch.h"
#include "game/text_ids.h"
#include "test_support.h"

#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {
item_effect change(const std::string& target, const effect_bucket bucket, const float value,
                   const effect_duration duration = effect_duration::seconds, const float seconds = 30.0f) {
    item_effect effect;
    effect.target = target;
    effect.bucket = bucket;
    effect.value = value;
    effect.duration = duration;
    effect.seconds = seconds;
    return effect;
}

item_definition item_named(const std::string& id, const item_kind kind, const std::string& name) {
    item_definition item;
    item.id = id;
    item.kind = kind;
    item.name = name;
    return item;
}

// A drinkable with three effects and a trinket worn for its effect.
item_catalog test_items() {
    item_catalog items;
    items.items["tonic"] = item_named("tonic", item_kind::drinkable, "item.beer.name");
    items.items["lucky_coin"] = item_named("lucky_coin", item_kind::trinket, "item.cigarette.name");
    return items;
}

active_effect started(const effect_source& source, const item_effect& effect, const std::int64_t from, const std::int64_t to) {
    return active_effect{source, effect, from, to};
}

const effect_catalog& shipped_events() {
    return shipped_content().effects;
}

// The tonic's three effects from 1 s to 31 s, the dazed effect from 5 s to 25 s, a coin worn since always.
std::vector<active_effect> three_sources() {
    const effect_source tonic = item_source("tonic");
    return {started(tonic, change(effect_target_walk_speed, effect_bucket::increased, 0.1f), 1000, 31000),
            started(tonic, change(effect_target_all_xp, effect_bucket::increased, 0.1f), 1000, 31000),
            started(tonic, change(effect_target_shot_spread, effect_bucket::more, -0.2f), 1000, 31000),
            started(event_source("dazed"), shipped_events().find("dazed")->effects[0], 5000, 25000),
            started(item_source("lucky_coin"), change(effect_target_cart_top_speed, effect_bucket::flat, 1.0f, effect_duration::equipped),
                    std::numeric_limits<std::int64_t>::min(), effect_never_ends)};
}
}

TEST_CASE("the shelf has one square per running source, newest first") {
    const std::vector<effect_square> squares = effect_shelf(three_sources(), test_items(), shipped_events(), 6000);
    REQUIRE(squares.size() == 3U);
    CHECK(squares[0].source == event_source("dazed"));
    CHECK(squares[1].source == item_source("tonic"));
    CHECK(squares[2].source == item_source("lucky_coin"));
    // An item with three effects is one square that lists all three.
    CHECK(squares[1].effects.size() == 3U);
    CHECK(squares[0].effects.size() == 1U);

    // The same id as an item and as an event is two sources.
    const std::vector<active_effect> both{started(item_source("dazed"), change(effect_target_all_xp, effect_bucket::flat, 1.0f), 0, 9000),
                                          started(event_source("dazed"), shipped_events().find("dazed")->effects[0], 0, 9000)};
    item_catalog items = test_items();
    items.items["dazed"] = item_named("dazed", item_kind::trinket, "item.beer.name");
    CHECK(effect_shelf(both, items, shipped_events(), 1000).size() == 2U);
}

TEST_CASE("a square names its source from its catalog: an event's own name, art and sting, an item's name and kind") {
    const std::vector<effect_square> squares = effect_shelf(three_sources(), test_items(), shipped_events(), 6000);
    REQUIRE(squares.size() == 3U);
    const event_effect_definition& dazed = *shipped_events().find("dazed");
    CHECK(squares[0].name == dazed.name);
    CHECK(squares[0].description == dazed.description);
    CHECK(squares[0].icon == dazed.icon);
    CHECK(squares[0].harmful);

    CHECK(squares[1].name == "item.beer.name");
    CHECK(squares[1].icon == "drinkable");
    CHECK(squares[2].icon == "trinket");
    CHECK(squares[1].description.empty());
    CHECK(!squares[1].harmful);
    CHECK(!squares[2].harmful);
}

TEST_CASE("seconds left are rounded up, and an equipped item's effects have none") {
    const std::vector<effect_square> at_6s = effect_shelf(three_sources(), test_items(), shipped_events(), 6000);
    REQUIRE(at_6s.size() == 3U);
    CHECK(at_6s[0].seconds_left == std::optional<int>(19));
    CHECK(at_6s[1].seconds_left == std::optional<int>(25));
    CHECK(!at_6s[2].seconds_left.has_value());

    // A hair into a second still counts the whole second it is in.
    const std::vector<effect_square> at_6001 = effect_shelf(three_sources(), test_items(), shipped_events(), 6001);
    CHECK(at_6001[0].seconds_left == std::optional<int>(19));
    CHECK(at_6001[1].seconds_left == std::optional<int>(25));
    const std::vector<effect_square> last_ms = effect_shelf(three_sources(), test_items(), shipped_events(), 24999);
    REQUIRE(last_ms.size() == 3U);
    CHECK(last_ms[0].seconds_left == std::optional<int>(1));
    CHECK(last_ms[0].source == event_source("dazed"));
}

TEST_CASE("ended and unknown sources have no square") {
    // Dazed has ended by 25 s; the tonic and the coin are left.
    const std::vector<effect_square> after = effect_shelf(three_sources(), test_items(), shipped_events(), 25000);
    REQUIRE(after.size() == 2U);
    CHECK(after[0].source == item_source("tonic"));
    CHECK(effect_shelf(three_sources(), test_items(), shipped_events(), 40000).size() == 1U);
    // Not yet started.
    CHECK(effect_shelf(three_sources(), test_items(), shipped_events(), 0).size() == 1U);

    CHECK(effect_shelf(three_sources(), item_catalog{}, effect_catalog{}, 6000).empty());
    CHECK(effect_shelf(three_sources(), test_items(), effect_catalog{}, 6000).size() == 2U);
}

TEST_CASE("a source refreshed later is newer than one started in between") {
    const std::vector<active_effect> effects{
        started(item_source("tonic"), change(effect_target_all_xp, effect_bucket::flat, 1.0f), 1000, 40000),
        started(item_source("lucky_coin"), change(effect_target_all_xp, effect_bucket::flat, 1.0f), 2000, 40000),
        started(item_source("tonic"), change(effect_target_walk_speed, effect_bucket::flat, 1.0f), 3000, 40000)};
    const std::vector<effect_square> squares = effect_shelf(effects, test_items(), shipped_events(), 5000);
    REQUIRE(squares.size() == 2U);
    CHECK(squares[0].source == item_source("tonic"));
    CHECK(squares[1].source == item_source("lucky_coin"));
}

TEST_CASE("seconds show as numbers up to 99 and whole minutes rounded up after") {
    const text_assets& text = shipped_text_assets();
    CHECK(effect_seconds_text(text, 1) == "1");
    CHECK(effect_seconds_text(text, 45) == "45");
    CHECK(effect_seconds_text(text, 99) == "99");
    CHECK(effect_seconds_text(text, 100) == "2M");
    CHECK(effect_seconds_text(text, 120) == "2M");
    CHECK(effect_seconds_text(text, 121) == "3M");
    CHECK(effect_seconds_text(text, 3600) == "60M");
}

TEST_CASE("an effect's change line has a signed whole percent or a signed number and the target's name") {
    const text_assets& text = shipped_text_assets();
    CHECK(effect_change_text(text, change(effect_target_walk_speed, effect_bucket::increased, -0.3f)) == "-30% WALKING SPEED");
    CHECK(effect_change_text(text, change(effect_target_all_xp, effect_bucket::increased, 0.1f)) == "+10% ALL XP");
    CHECK(effect_change_text(text, change("xp.golf_swing", effect_bucket::increased, 0.1f)) == "+10% GOLF SWING XP");
    CHECK(effect_change_text(text, change(effect_target_cart_top_speed, effect_bucket::more, 0.25f)) == "+25% CART TOP SPEED");
    CHECK(effect_change_text(text, change(effect_target_shot_spread, effect_bucket::more, -0.2f)) == "-20% SHOT SPREAD");
    CHECK(effect_change_text(text, change(effect_target_cart_uphill, effect_bucket::flat, 5.0f)) == "+5 CART CLIMBING");
    CHECK(effect_change_text(text, change(effect_target_cart_drift, effect_bucket::flat, -0.5f)) == "-0.5 CART GRIP");
    CHECK(effect_change_text(text, change(effect_target_all_xp, effect_bucket::flat, 2.25f)) == "+2.25 ALL XP");
}

TEST_CASE("every effect target gameplay reads has a name in the string table") {
    const text_assets& text = shipped_text_assets();
    for (const char* target : {effect_target_walk_speed, effect_target_cart_top_speed, effect_target_cart_uphill,
                               effect_target_cart_drift, effect_target_shot_spread, effect_target_shot_club_speed,
                               effect_target_all_xp}) {
        const std::string name = effect_target_text(text, target);
        CHECK(!name.empty());
        CHECK(name[0] != '#');
    }
    for (const skill_definition& skill : shipped_content().skills.skills) {
        CHECK(effect_target_text(text, effect_target_xp_prefix + skill.id).find('#') == std::string::npos);
    }
    // A target without a name is marked, not hidden.
    CHECK(effect_target_text(text, "player.nothing").find('#') != std::string::npos);
    CHECK(format_text(text, text_hud_effect_more, {{"count", "3"}}) == "+3");
}

TEST_CASE("the frame's shelf carries each square's words resolved from the strings") {
    const text_assets& text = shipped_text_assets();
    game_state state = started_hole();
    state.event_effects = shipped_events();
    CHECK(effect_shelf(current_effects(state), state.items, state.event_effects, shot_unix_ms(state)).empty());
    {
        render_data none;
        add_effect_shelf(none, state, text);
        CHECK(none.effect_squares.empty());
    }

    state.effects = effects_after_event({}, *state.event_effects.find("dazed"), shot_unix_ms(state));
    render_data data;
    add_effect_shelf(data, state, text);
    REQUIRE(data.effect_squares.size() == 1U);
    const render_effect_square& dazed = data.effect_squares[0];
    CHECK(dazed.name == "DAZED");
    CHECK(dazed.icon == "dazed");
    CHECK(dazed.harmful);
    CHECK(dazed.seconds == "20");
    CHECK(!dazed.description.empty());
    CHECK(dazed.description[0] != '#');
    REQUIRE(dazed.changes.size() == 1U);
    CHECK(dazed.changes[0] == "-30% WALKING SPEED");
}
