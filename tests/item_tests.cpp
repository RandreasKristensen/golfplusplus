#include "doctest.h"

#include "game/effects.h"
#include "game/item_catalog.h"
#include "game/lighting_loader.h"
#include "game/loadout.h"
#include "game/mode_dispatch.h"
#include "game/save_data.h"
#include "game/server_errors.h"
#include "game/sun_lighting.h"
#include "play_rules.h"
#include "renderer/prop_light.h"
#include "server_test_support.h"
#include "test_support.h"

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
item_effect effect_on(const std::string& target, const effect_bucket bucket, const float value,
                      const effect_duration duration = effect_duration::equipped) {
    item_effect effect;
    effect.target = target;
    effect.bucket = bucket;
    effect.value = value;
    effect.duration = duration;
    return effect;
}

active_effect running(const item_effect& effect, const std::int64_t from = 0, const std::int64_t to = effect_never_ends) {
    return active_effect{"test_item", effect, from, to};
}

// Items as their files say, keyed by path under items_path.
std::map<std::string, std::string> item_files() {
    return {
        {"smokable/cigarette.json",
         R"({"model": "cigarette", "name": "item.cigarette.name", "stacking": "refresh",
             "use": {"seconds_per_use": 2.0, "xp_per_use": {"skill": "smoking", "xp": 10}, "sound": "emote_smoke"},
             "effects": [{"target": "xp.smoking", "bucket": "increased", "value": 0.5, "duration": "use"}]})"},
        {"smokable/premium_cigarette.json",
         R"({"base": "cigarette", "model": "cigarette_gold", "name": "item.premium_cigarette.name",
             "use": {"seconds_per_use": 16.0},
             "effects": [{"target": "xp.all", "bucket": "increased", "value": 0.2, "duration": {"seconds": 60}}]})"},
        {"drinkable/beer.json",
         R"({"model": "beer_can", "name": "item.beer.name", "stacking": "stack",
             "use": {"seconds_per_use": 2.0, "sound": "emote_beer"},
             "effects": [{"target": "xp.all", "bucket": "increased", "value": 0.1, "duration": {"seconds": 30}}]})"},
        {"cart/drift_cart.json",
         R"({"model": "golf_cart", "name": "item.drift_cart.name", "stacking": "refresh",
             "effects": [{"target": "cart.drift", "bucket": "flat", "value": 1, "duration": "equipped"}],
             "spec": {"mass_kg": 450, "motor_power_w": 3500, "max_drive_force_n": 2000, "reverse_share": 0.5,
                      "top_speed_mps": 5.8, "wheelbase_m": 1.65, "track_m": 0.95, "cg_height_m": 0.64,
                      "max_steer_degrees": 30, "tip_seconds": 0.25, "wade_depth_m": 0.3, "right_seconds": 4,
                      "tow_seconds": 60}})"},
        {"bag/golf_bag.json", R"({"model": "golf_bag", "name": "item.golf_bag.name", "stacking": "refresh", "effects": []})"},
    };
}

item_catalog test_items() {
    return *parse_item_catalog(item_files()).catalog;
}
}

TEST_CASE("a final value is base plus flat, times one plus every increased, times each more") {
    const std::vector<active_effect> effects{
        running(effect_on("cart.top_speed", effect_bucket::flat, 2.0f)),
        running(effect_on("cart.top_speed", effect_bucket::flat, 1.0f)),
        running(effect_on("cart.top_speed", effect_bucket::increased, 0.25f)),
        running(effect_on("cart.top_speed", effect_bucket::increased, 0.25f)),
        running(effect_on("cart.top_speed", effect_bucket::more, 0.5f)),
        running(effect_on("cart.top_speed", effect_bucket::more, -0.5f)),
        running(effect_on("player.walk_speed", effect_bucket::flat, 100.0f)),
    };
    // (10 + 3) x (1 + 0.5) x 1.5 x 0.5: increased add up, more multiply.
    CHECK(near(effective_value(10.0f, "cart.top_speed", effects, 0), 13.0f * 1.5f * 1.5f * 0.5f, 0.0001f));
    CHECK(effective_value(10.0f, "cart.uphill", effects, 0) == 10.0f);
    CHECK(effective_value(4.0f, "player.walk_speed", effects, 0) == 104.0f);

    // Only while it runs: from its start up to (not at) its end.
    const std::vector<active_effect> timed{running(effect_on("cart.top_speed", effect_bucket::flat, 1.0f), 1000, 2000)};
    CHECK(effective_value(1.0f, "cart.top_speed", timed, 999) == 1.0f);
    CHECK(effective_value(1.0f, "cart.top_speed", timed, 1000) == 2.0f);
    CHECK(effective_value(1.0f, "cart.top_speed", timed, 1999) == 2.0f);
    CHECK(effective_value(1.0f, "cart.top_speed", timed, 2000) == 1.0f);

    // XP for a skill counts its own target and every skill's, rounded.
    const std::vector<active_effect> xp{running(effect_on("xp.smoking", effect_bucket::increased, 0.5f)),
                                        running(effect_on("xp.all", effect_bucket::increased, 0.25f))};
    CHECK(effective_reward(xp_reward{"smoking", 10}, xp, 0).xp == 18);
    CHECK(effective_reward(xp_reward{"fitness", 10}, xp, 0).xp == 13);
}

TEST_CASE("using an item again restarts its effects with refresh, adds another with stack") {
    const item_catalog items = test_items();
    const item_definition& cigarette = *items.find("cigarette");
    const item_definition& beer = *items.find("beer");

    // `use` effects last the use; {"seconds": n} its seconds.
    std::vector<active_effect> effects = effects_after_use({}, cigarette, 1000);
    REQUIRE(effects.size() == 1U);
    CHECK(effects[0].started_at_ms == 1000);
    CHECK(effects[0].ends_at_ms == 3000);
    effects = effects_after_use(effects, cigarette, 2500);
    REQUIRE(effects.size() == 1U);
    CHECK(effects[0].started_at_ms == 2500);
    CHECK(effects[0].ends_at_ms == 4500);

    effects = effects_after_use(effects, beer, 3000);
    effects = effects_after_use(effects, beer, 3500);
    CHECK(effects.size() == 3U);
    CHECK(near(effective_value(10.0f, "xp.all", effects, 4000), 12.0f, 0.0001f));
    // Ended effects go.
    CHECK(running_effects(effects, 33100).size() == 1U);
    CHECK(running_effects(effects, 34000).empty());
}

TEST_CASE("items parse with their kind from their folder, and a variant overrides only what it names") {
    const item_catalog items = test_items();
    REQUIRE(items.items.size() == 5U);
    const item_definition& cigarette = *items.find("cigarette");
    const item_definition& premium = *items.find("premium_cigarette");
    CHECK(cigarette.kind == item_kind::smokable);
    CHECK(items.find("beer")->kind == item_kind::drinkable);
    CHECK(items.find("drift_cart")->kind == item_kind::cart);
    // The premium cigarette keeps its base's XP per use and sound, plays longer, and does its own thing.
    CHECK(premium.model == "cigarette_gold");
    CHECK(premium.use->seconds_per_use == 16.0f);
    CHECK(premium.use->xp_per_use->xp == 10);
    CHECK(premium.use->sound == "emote_smoke");
    CHECK(premium.stacking == item_stacking::refresh);
    REQUIRE(premium.effects.size() == 1U);
    CHECK(premium.effects[0].target == "xp.all");
    CHECK(premium.effects[0].duration == effect_duration::seconds);
    CHECK(premium.effects[0].seconds == 60.0f);

    // A cart's whole spec, so a cart is refused only for what each case breaks.
    const std::string cart = R"("spec": {"mass_kg": 450, "motor_power_w": 3500, "max_drive_force_n": 2000,
        "reverse_share": 0.5, "top_speed_mps": 5.8, "wheelbase_m": 1.65, "track_m": 0.95, "cg_height_m": 0.64,
        "max_steer_degrees": 30, "tip_seconds": 0.25, "wade_depth_m": 0.3, "right_seconds": 4, "tow_seconds": 60}, )";
    const auto refused = [](const std::string& path, const std::string& text) {
        return !parse_item_catalog({{path, text}}).catalog.has_value();
    };
    // A smokable or drinkable needs its use; a cart has none.
    CHECK(refused("smokable/a.json", R"({"model": "m", "name": "n", "stacking": "refresh", "effects": []})"));
    CHECK(refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh", "effects": [],
        "use": {"seconds_per_use": 1, "sound": "s"}})"));
    // An unknown kind, bucket, duration or stacking.
    CHECK(refused("hat/a.json", R"({"model": "m", "name": "n", "stacking": "refresh", "effects": []})"));
    CHECK(refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh",
        "effects": [{"target": "cart.drift", "bucket": "most", "value": 1, "duration": "equipped"}]})"));
    CHECK(refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh",
        "effects": [{"target": "cart.drift", "bucket": "flat", "value": 1, "duration": "forever"}]})"));
    CHECK(refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "pile", "effects": []})"));
    // A `use` effect on an item that is not used.
    CHECK(refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh",
        "effects": [{"target": "cart.drift", "bucket": "flat", "value": 1, "duration": "use"}]})"));
    // A base of another kind is not found.
    CHECK(!parse_item_catalog({{"cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh", "effects": []})"},
                               {"trinket/b.json", R"({"base": "a"})"}})
               .catalog.has_value());
    // A cart needs its whole spec, within real limits.
    CHECK(refused("cart/a.json", R"({"model": "m", "name": "n", "stacking": "refresh", "effects": []})"));
    std::string slow = cart;
    slow.replace(slow.find("5.8"), 3, "0.1");
    CHECK(refused("cart/a.json", "{" + slow + R"("model": "m", "name": "n", "stacking": "refresh", "effects": []})"));
    CHECK(!refused("cart/a.json", "{" + cart + R"("model": "m", "name": "n", "stacking": "refresh", "effects": []})"));
}

TEST_CASE("a club item needs its whole spec within real limits, and a variant changes only what it names") {
    const std::string spec = R"("spec": {"static_loft_degrees": 31, "lie_degrees": 62.5, "length_meters": 0.93,
        "effective_mass_kg": 0.29, "face_cor": 0.74, "face_friction": 0.4, "face_grip": 0.27, "face_share": 0.75, "spin_scale": 1,
        "gear_effect": 0, "face_centre_height_meters": 0.019, "delivered_loft_degrees": 22.8, "speed_at_99": 42.4,
        "stock_attack_degrees": -3.9,
        "stock_ball_position": 0, "stock_backswing": 1, "sole_bounce_degrees": 4, "forgiveness": 0.5,
        "max_face_open_degrees": 22, "max_face_closed_degrees": 15, "sand_face_open_degrees": 0})";
    const std::string head = R"({"model": "club_iron", "name": "item.seven_iron.name", "label": "7I",
        "class": "short_irons", "hit_sound": "club_hit_iron", "mishit_sound": "club_mishit_iron", "bag_order": 6,
        "stacking": "refresh", "effects": [], )";
    const item_catalog_load_result parsed = parse_item_catalog(
        {{"club/seven_iron.json", head + spec + "}"},
         {"club/strong_seven.json", R"({"base": "seven_iron", "label": "S7", "spec": {"static_loft_degrees": 28}})"}});
    REQUIRE(parsed.catalog.has_value());
    const std::vector<club_definition> clubs = clubs_of(*parsed.catalog);
    REQUIRE(clubs.size() == 2U);
    const club_definition& seven = *parsed.catalog->find("seven_iron")->club;
    const club_definition& strong = *parsed.catalog->find("strong_seven")->club;
    CHECK(seven.id == "seven_iron");
    CHECK(seven.name == "item.seven_iron.name");
    CHECK(strong.spec.static_loft_degrees == 28.0f);
    CHECK(strong.spec.face_cor == seven.spec.face_cor);

    const auto refused = [](const std::string& text) { return !parse_item_catalog({{"club/a.json", text}}).catalog; };
    // No spec, a class that is not a skill, or a face livelier than the Rules allow (0.830).
    CHECK(refused(head + R"("spec": {}})"));
    CHECK(refused(R"({"model": "club_iron", "name": "n", "label": "7I", "class": "irons", "hit_sound": "h",
        "mishit_sound": "m", "bag_order": 6, "stacking": "refresh", "effects": [], )" + spec + "}"));
    std::string hot = spec;
    hot.replace(hot.find("\"face_cor\": 0.74"), 16, "\"face_cor\": 0.86");
    CHECK(refused(head + hot + "}"));
}

TEST_CASE("the loadout takes the default for any slot that does not fit, and its equipped effects run forever") {
    const item_catalog items = test_items();
    const loadout defaults{"drift_cart", {}, "cigarette", "beer", "golf_bag"};
    REQUIRE(loadout_fits(defaults, items));
    CHECK(!loadout_fits(loadout{"cigarette", {}, "cigarette", "beer", "golf_bag"}, items));
    CHECK(!loadout_fits(loadout{"drift_cart", {}, "cigarette", "beer", "beer"}, items));
    CHECK(!loadout_fits(loadout{"drift_cart", {{"mirror", "beer"}}, "cigarette", "beer", "golf_bag"}, items));

    const loadout saved{"no_such_cart", {}, "premium_cigarette", ""};
    const loadout fitted = fitted_loadout(saved, defaults, items);
    CHECK(fitted.cart == "drift_cart");
    CHECK(fitted.smokable == "premium_cigarette");
    CHECK(fitted.drinkable == "beer");

    const std::vector<active_effect> equipped = equipped_effects(defaults, items);
    REQUIRE(equipped.size() == 1U);
    CHECK(equipped[0].item == "drift_cart");
    CHECK(effective_value(0.0f, "cart.drift", equipped, 0) == 1.0f);
    CHECK(effective_value(0.0f, "cart.drift", equipped, 9000000000000) == 1.0f);

    const std::optional<loadout> parsed =
        parse_loadout_from_text(R"({"cart": "drift_cart", "cart_mounts": {"mirror": "dice"}, "smokable": "cigarette", "drinkable": "beer", "bag": "golf_bag"})");
    REQUIRE(parsed.has_value());
    CHECK(parsed->cart_mounts.at("mirror") == "dice");
    CHECK(!parse_loadout_from_text(R"({"cart": "drift_cart", "smokable": "cigarette", "drinkable": "beer"})").has_value());
}

TEST_CASE("the save keeps what is equipped, and a save without it plays with the defaults") {
    save_data save;
    save.equipped = loadout{"golf_cart", {{"mirror", "dice"}}, "cigarette", "beer", "golf_bag"};
    const std::optional<save_data> back = parse_save_data(save_data_to_json(save));
    REQUIRE(back.has_value());
    CHECK(back->equipped.cart == "golf_cart");
    CHECK(back->equipped.cart_mounts.at("mirror") == "dice");
    CHECK(back->equipped.smokable == "cigarette");
    CHECK(back->equipped.drinkable == "beer");
    CHECK(back->equipped.bag == "golf_bag");

    const game_content& content = shipped_content();
    const game_state state = make_game_state(content, *parse_save_data(R"({"version": 6})"));
    CHECK(state.save.equipped.cart == content.default_loadout.cart);
    CHECK(state.save.equipped.smokable == content.default_loadout.smokable);
}

TEST_CASE("an item's effect ends at the same moment offline and on the server") {
    game_state state = started_hole();
    state.local_unix_ms = 1800000000000;
    state.items = test_items();
    state.default_loadout = loadout{"drift_cart", {}, "premium_cigarette", "beer", "golf_bag"};
    state.save.equipped = state.default_loadout;
    const item_definition& premium = *state.items.find("premium_cigarette");
    const std::int64_t now = shot_unix_ms(state);
    record_item_use(state, premium);
    // The server's use_item runs effects_after_use on its own clock: the same ends.
    const std::vector<active_effect> on_server = effects_after_use({}, premium, now);
    REQUIRE(state.effects.size() == on_server.size());
    for (std::size_t i = 0; i < on_server.size(); ++i) {
        CHECK(state.effects[i].ends_at_ms == on_server[i].ends_at_ms);
        CHECK(state.effects[i].started_at_ms == on_server[i].started_at_ms);
    }
    CHECK(state.effects.front().ends_at_ms == now + 60000);
    // And the server lets it be used again once the use it timed is over.
    server_content content = fixture_server_content();
    content.items = state.items;
    const std::int64_t use_ms = static_cast<std::int64_t>(premium.use->seconds_per_use * 1000.0f);
    const std::int64_t slack_ms = static_cast<std::int64_t>(content.tuning.server.timing_slack_seconds * 1000.0f);
    CHECK(check_item_use(premium.id, state.default_loadout, now, now + use_ms - slack_ms - 1, content) == error_too_soon);
    CHECK(check_item_use(premium.id, state.default_loadout, now, now + use_ms, content).empty());
}

TEST_CASE("a held prop is lit by the sun on the side facing it, and at night by a floodlight beside it") {
    const lighting_palette& palette = *load_lighting(asset_root());
    // Midday: the sun high to the south.
    const lighting_key noon = lighting_at(palette, 50.0f);
    frame_lighting_block day;
    day.sun_direction = glm::vec4(glm::normalize(glm::vec3(0.0f, std::sin(glm::radians(50.0f)), -std::cos(glm::radians(50.0f)))), 0.0f);
    day.sun_color = glm::vec4(noon.sun_color, 0.0f);
    day.sky_light = glm::vec4(noon.ambient_sky, 0.0f);
    day.ground_light = glm::vec4(noon.ambient_ground, 0.0f);
    const glm::vec3 can(0.0f, 1.5f, 0.0f);
    const glm::vec3 towards_sun(day.sun_direction);
    // The camera stands where it sees both faces.
    day.haze_eye = glm::vec4(can + glm::vec3(3.0f, 0.0f, 0.0f), 0.0f);
    const glm::vec3 facing = prop_light(day, can, glm::normalize(glm::vec3(towards_sun.x, 0.0f, towards_sun.z) + glm::vec3(0.2f, 0.0f, 0.0f)), 1.0f, 0.0f);
    const glm::vec3 away = prop_light(day, can, glm::normalize(-glm::vec3(towards_sun.x, 0.0f, towards_sun.z) + glm::vec3(0.2f, 0.0f, 0.0f)), 1.0f, 0.0f);
    CHECK(luminance(facing) > 1.2f * luminance(away));
    // In the shadow map's shadow it loses the sun.
    CHECK(luminance(prop_light(day, can, glm::vec3(0.0f, 0.0f, -1.0f), 0.0f, 0.0f)) <
          luminance(prop_light(day, can, glm::vec3(0.0f, 0.0f, -1.0f), 1.0f, 0.0f)));

    // Night: the sky's little light, and a floodlight shining at one of two props.
    const lighting_key midnight = lighting_at(palette, -90.0f);
    frame_lighting_block night;
    night.sun_direction = glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
    night.sun_color = glm::vec4(midnight.sun_color, 0.0f);
    night.sky_light = glm::vec4(midnight.ambient_sky, 0.0f);
    night.ground_light = glm::vec4(midnight.ambient_ground, 0.0f);
    const night_light_style& lights = palette.night_lights;
    const glm::vec3 lamp(0.0f, 8.0f, 0.0f);
    const glm::vec3 lit_prop(0.0f, 1.0f, 3.0f);
    const glm::vec3 dark_prop(200.0f, 1.0f, 3.0f);
    night.haze_eye = glm::vec4(lit_prop + glm::vec3(0.0f, 0.0f, 3.0f), 0.0f);
    night.light_position[0] = glm::vec4(lamp, lights.floodlight.radius);
    night.light_color[0] = glm::vec4(lights.floodlight.color * lights.floodlight.intensity,
                                     std::cos(glm::radians(lights.floodlight_cone_deg - lights.floodlight_soft_deg)));
    night.light_cone[0] = glm::vec4(glm::normalize(lit_prop - lamp), std::cos(glm::radians(lights.floodlight_cone_deg)));
    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    CHECK(luminance(prop_light(night, lit_prop, up, 0.0f, 0.0f)) > 2.0f * luminance(prop_light(night, dark_prop, up, 0.0f, 0.0f)));
    // And a glow floor shows in the dark whatever lights it.
    CHECK(prop_light(night, dark_prop, up, 0.0f, 0.8f).r >= 0.8f);
}
