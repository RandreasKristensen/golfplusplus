#include "doctest.h"

#include "game/effect_catalog.h"
#include "game/effects.h"
#include "game/item_catalog.h"
#include "test_support.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
const std::string good_effect = R"({
    "name": "effect.dazed.name", "description": "effect.dazed.description", "icon": "dazed", "harmful": true,
    "effects": [{"target": "player.walk_speed", "bucket": "increased", "value": -0.3, "duration": {"seconds": 20}}],
    "screen": {"vignette": {"colour": [0.75, 0.04, 0.03], "seconds": 2.0}}})";

event_effect_definition dazed() {
    return *parse_event_effect("dazed", good_effect);
}

// A copy of the good file with `from` replaced by `to`.
std::string changed(const std::string& from, const std::string& to) {
    std::string text = good_effect;
    const std::size_t at = text.find(from);
    CHECK(at != std::string::npos);
    return at == std::string::npos ? text : text.replace(at, from.size(), to);
}
}

TEST_CASE("an event effect file parses with its entries and its vignette") {
    const std::optional<event_effect_definition> effect = parse_event_effect("dazed", good_effect);
    REQUIRE(effect);
    CHECK(effect->id == "dazed");
    CHECK(effect->name == "effect.dazed.name");
    CHECK(effect->description == "effect.dazed.description");
    CHECK(effect->icon == "dazed");
    CHECK(effect->harmful);
    REQUIRE(effect->effects.size() == 1U);
    CHECK(effect->effects[0].target == effect_target_walk_speed);
    CHECK(effect->effects[0].bucket == effect_bucket::increased);
    CHECK(effect->effects[0].duration == effect_duration::seconds);
    CHECK(effect->effects[0].seconds == 20.0f);
    REQUIRE(effect->vignette);
    CHECK(effect->vignette->seconds == 2.0f);
    CHECK(near(effect->vignette->colour.x, 0.75f, 0.0001f));

    const std::string without_screen = R"({"name": "n", "description": "d", "icon": "i", "harmful": false,
        "effects": [{"target": "xp.all", "bucket": "flat", "value": 1, "duration": {"seconds": 5}}]})";
    const std::optional<event_effect_definition> plain = parse_event_effect("plain", without_screen);
    REQUIRE(plain);
    CHECK(!plain->vignette);
    CHECK(!plain->harmful);
}

TEST_CASE("an event effect file refuses what is missing or wrong") {
    for (const char* key : {"name", "description", "icon", "harmful", "effects"}) {
        json root = *parse_json(good_effect);
        root.erase(key);
        CHECK(!parse_event_effect("x", root.dump()));
    }
    json empty_name = *parse_json(good_effect);
    empty_name["name"] = "";
    CHECK(!parse_event_effect("x", empty_name.dump()));
    json no_entries = *parse_json(good_effect);
    no_entries["effects"] = json::array();
    CHECK(!parse_event_effect("x", no_entries.dump()));
    CHECK(!parse_event_effect("x", "[]"));
    CHECK(!parse_event_effect("x", "not json"));

    // Event effects always last a number of seconds.
    CHECK(!parse_event_effect("x", changed(R"({"seconds": 20})", R"("instant")")));
    CHECK(!parse_event_effect("x", changed(R"({"seconds": 20})", R"("use")")));
    CHECK(!parse_event_effect("x", changed(R"({"seconds": 20})", R"("equipped")")));
    CHECK(!parse_event_effect("x", changed(R"({"seconds": 20})", R"({"seconds": 0})")));
    CHECK(!parse_event_effect("x", changed(R"("bucket": "increased")", R"("bucket": "total")")));

    // A screen needs a vignette with a colour in 0 to 1 and seconds above 0.
    CHECK(!parse_event_effect("x", changed(R"("vignette": {"colour": [0.75, 0.04, 0.03], "seconds": 2.0})", R"("glow": {})")));
    CHECK(!parse_event_effect("x", changed("[0.75, 0.04, 0.03]", "[0.75, 0.04]")));
    CHECK(!parse_event_effect("x", changed("[0.75, 0.04, 0.03]", "[1.5, 0.04, 0.03]")));
    CHECK(!parse_event_effect("x", changed("[0.75, 0.04, 0.03]", "[0.75, -0.04, 0.03]")));
    CHECK(!parse_event_effect("x", changed(R"("seconds": 2.0)", R"("seconds": 0)")));
    CHECK(!parse_event_effect("x", changed(R"("seconds": 2.0)", R"("colour2": 1)")));
}

TEST_CASE("an effect catalog is keyed by file name and names the file that fails") {
    const effect_catalog_load_result loaded = parse_effect_catalog({{"dazed.json", good_effect}});
    REQUIRE(loaded.catalog);
    CHECK(loaded.catalog->find("dazed") != nullptr);
    CHECK(loaded.catalog->find("missing") == nullptr);

    const effect_catalog_load_result bad = parse_effect_catalog({{"dazed.json", good_effect}, {"bad.json", "{}"}});
    CHECK(!bad.catalog);
    CHECK(bad.error == std::string("cannot load ") + effects_path + "/bad.json");
    CHECK(!parse_effect_catalog({{"folder/dazed.json", good_effect}}).catalog);
    CHECK(!parse_effect_catalog({{"dazed.txt", good_effect}}).catalog);
    CHECK(!parse_effect_catalog({}).catalog);
}

TEST_CASE("an event effect starts for its seconds and a second one refreshes the first") {
    const event_effect_definition effect = dazed();
    const effect_source source = event_source("dazed");
    std::vector<active_effect> running = effects_after_event({}, effect, 5000);
    REQUIRE(running.size() == 1U);
    CHECK(running[0].source == source);
    CHECK(running[0].started_at_ms == 5000);
    CHECK(running[0].ends_at_ms == 25000);
    CHECK(event_started_at(running, "dazed", 24999) == 5000);
    CHECK(!event_started_at(running, "dazed", 25000));
    CHECK(!event_started_at(running, "dazed", 4999));
    CHECK(!event_started_at(running, "other", 6000));

    running = effects_after_event(running, effect, 10000);
    REQUIRE(running.size() == 2U);
    CHECK(running[0].started_at_ms == 5000);
    CHECK(running[0].ends_at_ms == 10000);  // the first ends where the refresh starts
    CHECK(running[1].started_at_ms == 10000);
    CHECK(running[1].ends_at_ms == 30000);
    CHECK(near(effective_value(4.0f, effect_target_walk_speed, running, 11000), 2.8f, 0.0001f));
    CHECK(effective_value(4.0f, effect_target_walk_speed, running, 9999) == effective_value(4.0f, effect_target_walk_speed, running, 5000));
    CHECK(effect_running(running[0], 9999));
    CHECK(!effect_running(running[0], 10000));

    // An item's effects are not the event's.
    std::vector<active_effect> mixed{active_effect{item_source("dazed"), effect.effects[0], 0, 100000}};
    mixed = effects_after_event(mixed, effect, 10000);
    CHECK(mixed.size() == 2U);
}

TEST_CASE("a refresh at a future start leaves the time before it dazed, and never stacks") {
    const event_effect_definition effect = dazed();
    // Dazed at 5 s for 20 s; a second ball arrives at 12 s.
    std::vector<active_effect> running = effects_after_event({}, effect, 5000);
    running = effects_after_event(running, effect, 12000);
    for (const std::int64_t at : {5000, 8000, 11999, 12000, 20000, 31999}) {
        int running_now = 0;
        for (const active_effect& each : running) {
            running_now += effect_running(each, at) ? 1 : 0;
        }
        CHECK(running_now == 1);
    }
    CHECK(!event_started_at(running, "dazed", 32000));
    CHECK(event_started_at(running, "dazed", 8000) == 5000);
    CHECK(event_started_at(running, "dazed", 12500) == 12000);
    // An earlier-arriving ball than one already queued ahead drops the later one.
    running = effects_after_event(running, effect, 9000);
    REQUIRE(running.size() == 2U);
    CHECK(running[0].ends_at_ms == 9000);
    CHECK(running[1].started_at_ms == 9000);
}

TEST_CASE("effect sources are equal by kind and id") {
    CHECK(item_source("a") == item_source("a"));
    CHECK(!(item_source("a") == item_source("b")));
    CHECK(!(item_source("a") == event_source("a")));
}

TEST_CASE("an event effect and an item effect change a value the same way and stack") {
    const event_effect_definition effect = dazed();
    const item_effect same = effect.effects[0];
    const std::vector<active_effect> from_event = effects_after_event({}, effect, 0);
    const std::vector<active_effect> from_item{active_effect{item_source("slow_shoes"), same, 0, 20000}};
    for (const std::int64_t now : {0, 10000, 19999, 20000}) {
        CHECK(effective_value(4.0f, effect_target_walk_speed, from_event, now) ==
              effective_value(4.0f, effect_target_walk_speed, from_item, now));
    }

    std::vector<active_effect> both = from_event;
    both.push_back(from_item[0]);
    // 1 - 0.3 - 0.3 = 0.4.
    CHECK(near(effective_value(10.0f, effect_target_walk_speed, both, 1000), 4.0f, 0.0001f));
}

TEST_CASE("a cigarette quickens walking 5% and a beer the club 5% for 20 s, a second use restarting the 20 s") {
    const item_catalog& items = shipped_content().items;
    const std::vector<std::pair<const char*, const char*>> uses{{"cigarette", effect_target_walk_speed},
                                                                 {"beer", effect_target_shot_club_speed}};
    for (const auto& [id, target] : uses) {
        const item_definition& item = *items.find(id);
        std::vector<active_effect> effects = effects_after_use({}, item, 1000);
        CHECK(near(effective_value(10.0f, target, effects, 1000), 10.5f, 0.0001f));
        CHECK(effective_value(10.0f, target, effects, 21000) == 10.0f);
        effects = effects_after_use(effects, item, 15000);
        REQUIRE(effects.size() == 1U);
        CHECK(near(effective_value(10.0f, target, effects, 34999), 10.5f, 0.0001f));
        CHECK(effective_value(10.0f, target, effects, 35000) == 10.0f);
    }
}

TEST_CASE("the shipped event effects load, are named in the string table and dazed is as the owner decided") {
    const game_content& content = shipped_content();
    const text_assets& text = shipped_text_assets();
    REQUIRE(!content.effects.effects.empty());
    for (const auto& [id, effect] : content.effects.effects) {
        CHECK(text.strings.entries.count(effect.name) == 1U);
        CHECK(text.strings.entries.count(effect.description) == 1U);
    }
    const event_effect_definition* shipped = content.effects.find("dazed");
    REQUIRE(shipped != nullptr);
    CHECK(shipped->harmful);
    REQUIRE(shipped->effects.size() == 1U);
    CHECK(shipped->effects[0].target == effect_target_walk_speed);
    CHECK(shipped->effects[0].bucket == effect_bucket::increased);
    CHECK(near(shipped->effects[0].value, -0.3f, 0.0001f));
    CHECK(shipped->effects[0].duration == effect_duration::seconds);
    CHECK(shipped->effects[0].seconds == 20.0f);
    REQUIRE(shipped->vignette);
    CHECK(shipped->vignette->seconds == 2.0f);
    CHECK(shipped->vignette->colour.x > shipped->vignette->colour.y);
    CHECK(shipped->vignette->colour.x > shipped->vignette->colour.z);
}
