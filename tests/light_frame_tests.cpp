#include "doctest.h"

#include "core/light_frame.h"
#include "game/golf_cart.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>

namespace {
const lighting_palette& fixture_palette() {
    static const lighting_palette palette = *load_lighting(fixture_root());
    return palette;
}

// The way towards a sun `elevation_deg` up, from +x round towards +z by `azimuth_deg`.
glm::vec3 sun_at(const float elevation_deg, const float azimuth_deg = 0.0f) {
    const float elevation = glm::radians(elevation_deg);
    const float azimuth = glm::radians(azimuth_deg);
    return glm::vec3(std::cos(elevation) * std::cos(azimuth), std::sin(elevation), std::cos(elevation) * std::sin(azimuth));
}

// A frame on the fixture course, lit by `sun` with `sun_share` of its shadows and `night` of the night lights.
render_data lit_frame(const glm::vec3& sun, const float sun_share, const float night) {
    render_data data;
    data.lighting.sun_direction = sun;
    data.lighting.shadows = render_shadows{sun_share, fixture_palette().shadows.night_share, fixture_palette().shadows.max_stretch, 0.05f};
    data.lighting.night_lights = night;
    data.ball_visual_radius_meters = 0.1f;
    return data;
}

glm::vec3 above_ground(const game_state& game, const glm::vec3& spot, const float height) {
    return glm::vec3(spot.x, terrain_height(game.area, spot) + height, spot.z);
}
}

TEST_CASE("a blob is cast along the sun onto the ground, straight down without it") {
    const game_state game = started_game(fixture_hub_course());
    const glm::vec3 caster = above_ground(game, game.player.position, 4.0f);
    const glm::vec3 sun = sun_at(40.0f, 30.0f);

    // The landing spot lies back along the sun from the caster, on the ground.
    const terrain_sample cast = cast_blob_onto_ground(game.area, caster, sun, 1.0f, 5.0f);
    CHECK(near(glm::normalize(caster - cast.point), sun, 0.02f));
    CHECK(near(cast.point.y, terrain_height(game.area, cast.point), 0.001f));

    const terrain_sample below = cast_blob_onto_ground(game.area, caster, sun, 0.0f, 5.0f);
    CHECK(near(below.point.x, caster.x, 0.0001f));
    CHECK(near(below.point.z, caster.z, 0.0001f));

    // A low sun casts no longer than max_stretch allows.
    const terrain_sample low = cast_blob_onto_ground(game.area, caster, sun_at(1.0f), 1.0f, 5.0f);
    const glm::vec3 run = caster - low.point;
    CHECK(std::hypot(run.x, run.z) / run.y <= std::sqrt(24.0f) + 0.01f);
}

TEST_CASE("blob shadows fall under the ball and the player, darkest where they touch the ground at night") {
    const game_state game = started_game(fixture_hub_course());
    const shadow_style& style = fixture_palette().shadows;
    render_data day = lit_frame(sun_at(50.0f), 1.0f, 0.0f);
    day.show_ball = true;
    day.ball_position = above_ground(game, game.player.position + glm::vec3(3.0f, 0.0f, 0.0f), 10.0f);
    add_frame_lights(day, game, fixture_palette(), tree_shadow_cache{});
    REQUIRE(day.blob_shadows.size() == 2U);  // the ball, then me
    const render_blob_shadow& ball = day.blob_shadows[0];
    CHECK(near(ball.radius, 0.1f * style.ball_radius_scale, 0.0001f));
    CHECK(near(ball.strength, style.blob_strength, 0.0001f));
    CHECK(near(ball.stretch, 1.0f / std::sin(glm::radians(50.0f)), 0.05f));
    // Cast away from the sun (+x): the ball's blob lands on its -x side.
    CHECK(ball.base.x < day.ball_position.x - 5.0f);
    // Mine lies at my feet: the sun's shadow of me is the shadow map's.
    const render_blob_shadow& me = day.blob_shadows[1];
    CHECK(near(me.radius, style.player_radius, 0.0001f));
    CHECK(me.stretch == 1.0f);
    CHECK(near(me.strength, style.blob_strength * style.foot_share, 0.0001f));
    CHECK(near(horizontal(me.base), horizontal(game.player.position), 0.0001f));

    // At night the high ball darkens nothing; the player's feet still do.
    render_data night = lit_frame(sun_at(-20.0f), 0.0f, 1.0f);
    night.show_ball = true;
    night.ball_position = day.ball_position;
    add_frame_lights(night, game, fixture_palette(), tree_shadow_cache{});
    REQUIRE(night.blob_shadows.size() == 2U);
    CHECK(night.blob_shadows[0].strength == 0.0f);
    CHECK(near(night.blob_shadows[0].base.x, night.ball_position.x, 0.0001f));
    CHECK(near(night.blob_shadows[1].strength, style.blob_strength, 0.0001f));
}

TEST_CASE("a ball on the ground lies in its own shadow however low the sun") {
    const game_state game = started_game(fixture_hub_course());
    const float radius = 0.1f;
    for (const float elevation : {70.0f, 30.0f, 12.0f, 3.0f}) {
        render_data data = lit_frame(sun_at(elevation, 40.0f), 1.0f, 0.0f);
        data.show_ball = true;
        data.ball_position = above_ground(game, game.player.position + glm::vec3(4.0f, 0.0f, 0.0f), radius);
        add_frame_lights(data, game, fixture_palette(), tree_shadow_cache{});
        const render_blob_shadow& blob = data.blob_shadows[0];
        CHECK(blob.stretch >= 1.0f);
        // Where it touches the ground is well inside the blob, where it is still dark.
        const glm::vec3 contact = data.ball_position - world_up * radius;
        CHECK(horizontal_distance(contact, blob.base) < 0.6f * blob.radius * blob.stretch);
    }
}

TEST_CASE("a cart casts a cart's blob over whoever sits in it, and players on foot cast theirs") {
    game_state game = started_game(fixture_hub_course());
    game.ride = cart_ride{};  // at my cart's wheel
    const shadow_style& style = fixture_palette().shadows;
    render_data data = lit_frame(sun_at(60.0f), 1.0f, 0.0f);
    render_remote_avatar walker;
    walker.position = above_ground(game, game.player.position + glm::vec3(0.0f, 0.0f, 6.0f), 0.0f);
    render_remote_avatar driver = walker;
    driver.seated = true;
    driver.at_wheel = true;
    data.carts = {render_cart{game.cart.pose.position, game.cart.pose.yaw, true, 0.0f},
                  render_cart{driver.position, 0.0f, true, 0.0f}};
    data.remote_avatars = {walker, driver};
    add_frame_lights(data, game, fixture_palette(), tree_shadow_cache{});
    REQUIRE(data.blob_shadows.size() == 3U);
    CHECK(near(data.blob_shadows[0].radius, style.cart_radius, 0.0001f));   // mine, driven
    CHECK(near(data.blob_shadows[1].radius, style.cart_radius, 0.0001f));   // theirs, them in it
    CHECK(near(data.blob_shadows[2].radius, style.player_radius, 0.0001f));  // on foot
}

TEST_CASE("trees and floodlight poles darken the ground faintly at their foot, rebuilt only when the trees change") {
    game_state game = started_game(fixture_hub_course());
    const shadow_style& style = fixture_palette().shadows;
    const std::size_t poles = game.area.floodlights.size();
    REQUIRE(poles > 0U);
    tree_body tree;
    tree.base = above_ground(game, game.player.position + glm::vec3(5.0f, 0.0f, 0.0f), 0.0f);
    tree.shape = tree_shape{0.3f, 2.0f, 3.0f, 6.0f};
    game.static_anchors.trees = {tree, tree};
    game.static_anchors.revision = 7U;
    tree_shadow_cache cache;
    refresh_tree_shadows(cache, game, style);
    REQUIRE(cache.blobs.size() == 2U + poles);
    CHECK(cache.revision == 7U);
    CHECK(cache.blobs[0].stretch == 1.0f);
    CHECK(near(cache.blobs[0].radius, 3.0f * style.tree_radius_share, 0.0001f));
    CHECK(near(cache.blobs[0].strength, style.blob_strength * style.foot_share, 0.0001f));
    CHECK(near(cache.blobs[0].base, tree.base, 0.0001f));
    const render_blob_shadow& pole = cache.blobs[2];
    CHECK(near(pole.base, game.area.floodlights[0].foot, 0.0001f));
    CHECK(near(pole.radius, style.pole_radius, 0.0001f));

    game.static_anchors.trees.pop_back();
    refresh_tree_shadows(cache, game, style);
    CHECK(cache.blobs.size() == 2U + poles);  // same revision: kept
    game.static_anchors.revision = 8U;
    refresh_tree_shadows(cache, game, style);
    CHECK(cache.blobs.size() == 1U + poles);

    render_data data = lit_frame(sun_at(30.0f), 1.0f, 0.0f);
    add_frame_lights(data, game, fixture_palette(), cache);
    CHECK(data.tree_shadows == &cache.blobs);
    CHECK(data.tree_shadows_revision == 8U);
}

TEST_CASE("after dark the ball, carts' headlights and the floodlights light up, and the ball and lamps glow past white") {
    const game_state game = started_game(fixture_hub_course());
    const night_light_style& style = fixture_palette().night_lights;

    render_data day = lit_frame(sun_at(30.0f), 1.0f, 0.0f);
    day.show_ball = true;
    day.carts = {render_cart{game.cart.pose.position, game.cart.pose.yaw, true, 0.0f}};
    add_frame_lights(day, game, fixture_palette(), tree_shadow_cache{});
    CHECK(day.lights.empty());
    CHECK(day.glow.ball == fixture_palette().colors.glow.ball);
    CHECK(day.lighting.lamp_glow == glm::vec3(0.0f));

    render_data night = lit_frame(sun_at(-20.0f), 0.0f, 1.0f);
    night.show_ball = true;
    night.ball_position = above_ground(game, game.player.position, 0.1f);
    night.show_hole = true;
    night.pin_position = above_ground(game, game.player.position + glm::vec3(0.0f, 0.0f, 30.0f), 0.0f);
    // My cart, driven, and a parked one with its headlights off.
    night.carts = {render_cart{game.cart.pose.position, game.cart.pose.yaw, true, 0.0f},
                   render_cart{game.cart.pose.position + glm::vec3(5.0f, 0.0f, 0.0f), 0.0f, false, 0.0f}};
    add_frame_lights(night, game, fixture_palette(), tree_shadow_cache{});
    CHECK(night.glow.ball > 1.0f);
    CHECK(night.glow.ember > 1.0f);
    CHECK(near(night.lighting.lamp_glow, style.floodlight.color * style.lamp_glow, 0.0001f));
    CHECK(std::max({night.lighting.lamp_glow.r, night.lighting.lamp_glow.g, night.lighting.lamp_glow.b}) > 1.0f);
    const std::vector<floodlight>& lamps = game.area.floodlights;
    REQUIRE(!lamps.empty());
    REQUIRE(night.lights.size() == 2U + lamps.size());
    const render_light& ball = night.lights[0];
    CHECK(near(ball.position, night.ball_position, 0.0001f));
    CHECK(near(ball.color, style.ball.color * style.ball.intensity, 0.0001f));
    CHECK(ball.cone_edge_cos < -1.0f);  // every way
    const render_light& headlight = night.lights[1];
    const glm::vec3 ahead = yaw_direction(game.cart.pose.yaw);
    CHECK(glm::dot(headlight.direction, ahead) > 0.99f);
    CHECK(headlight.direction.y < 0.0f);
    CHECK(near(headlight.cone_edge_cos, std::cos(glm::radians(style.headlight_cone_deg)), 0.0001f));
    CHECK(headlight.cone_full_cos > headlight.cone_edge_cos);
    // Every lamp a spot from its head down along its aim; none over the cup.
    for (std::size_t i = 0; i < lamps.size(); ++i) {
        const render_light& lamp = night.lights[2 + i];
        CHECK(near(lamp.position, lamps[i].lamp, 0.0001f));
        CHECK(near(lamp.direction, lamps[i].aim, 0.0001f));
        CHECK(lamp.direction.y < 0.0f);
        CHECK(near(lamp.radius, style.floodlight.radius, 0.0001f));
        CHECK(near(lamp.cone_edge_cos, std::cos(glm::radians(style.floodlight_cone_deg)), 0.0001f));
        CHECK(lamp.cone_full_cos > lamp.cone_edge_cos);
    }
}

TEST_CASE("at dusk every floodlight is on as far as the night is") {
    const game_state game = started_game(fixture_hub_course());
    const night_light_style& style = fixture_palette().night_lights;
    render_data hub = lit_frame(sun_at(-20.0f), 0.0f, 0.5f);
    add_frame_lights(hub, game, fixture_palette(), tree_shadow_cache{});
    REQUIRE(hub.lights.size() == game.area.floodlights.size());
    for (const render_light& lamp : hub.lights) {
        CHECK(near(lamp.color, style.floodlight.color * style.floodlight.intensity * 0.5f, 0.0001f));
    }
    CHECK(near(hub.lighting.lamp_glow, style.floodlight.color * style.lamp_glow * 0.5f, 0.0001f));
}
