#include "core/light_frame.h"

#include "game/golf_cart.h"
#include "game/play_area.h"
#include "game/sun_lighting.h"
#include "physics/vector_math.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

namespace {
// Each guess at where the cast lands is corrected by the ground's height
// there this many times, enough on rolling ground.
constexpr int cast_refinements = 3;

// Something that casts a blob: its lowest point, how high over that it is
// cast from, and how wide its blob is.
struct blob_caster {
    glm::vec3 bottom{0.0f};
    float lift = 0.0f;
    float radius = 0.0f;
};

render_light point_light(const glm::vec3& position, const night_light& light, const float share) {
    render_light out;
    out.position = position;
    out.color = light.color * light.intensity * share;
    out.radius = light.radius;
    return out;
}

// `light` at `position` shining along `direction` in a cone `cone_deg` to its
// edge, full `soft_deg` in from it.
render_light spot_light(const glm::vec3& position, const glm::vec3& direction, const night_light& light, const float share,
                        const float cone_deg, const float soft_deg) {
    render_light out = point_light(position, light, share);
    out.direction = direction;
    out.cone_edge_cos = std::cos(glm::radians(cone_deg));
    out.cone_full_cos = std::cos(glm::radians(cone_deg - soft_deg));
    return out;
}

// A cart's headlight, the cart's middle at `cart` facing `yaw`.
render_light headlight(const glm::vec3& cart, const float yaw, const night_light_style& style, const float share) {
    const glm::vec3 forward = yaw_direction(yaw);
    const float pitch = glm::radians(style.headlight_pitch_deg);
    return spot_light(cart + forward * style.headlight_forward + world_up * style.headlight_height,
                      glm::normalize(forward * std::cos(pitch) - world_up * std::sin(pitch)), style.headlight, share,
                      style.headlight_cone_deg, style.headlight_soft_deg);
}

std::vector<render_light> night_lights(const render_data& data, const game_state& game, const night_light_style& style) {
    std::vector<render_light> lights;
    const float share = data.lighting.night_lights;
    if (share <= 0.0f) {
        return lights;
    }
    if (data.show_ball) {
        lights.push_back(point_light(data.ball_position, style.ball, share));
    }
    for (const render_remote_ball& ball : data.remote_balls) {
        lights.push_back(point_light(ball.position, style.ball, share));
    }
    for (const render_cart& cart : data.carts) {
        if (cart.driven) {
            lights.push_back(headlight(cart.position, cart.yaw, style, share));
        }
    }
    for (const floodlight& lamp : game.area.floodlights) {
        lights.push_back(
            spot_light(lamp.lamp, lamp.aim, style.floodlight, share, style.floodlight_cone_deg, style.floodlight_soft_deg));
    }
    return lights;
}

std::vector<blob_caster> frame_casters(const render_data& data, const game_state& game, const shadow_style& style) {
    std::vector<blob_caster> casters;
    const float ball_radius = data.ball_visual_radius_meters;
    const auto ball = [&](const glm::vec3& center) {
        casters.push_back(blob_caster{center - world_up * ball_radius, ball_radius, ball_radius * style.ball_radius_scale});
    };
    if (data.show_ball) {
        ball(data.ball_position);
    }
    for (const render_remote_ball& remote : data.remote_balls) {
        ball(remote.position);
    }
    // A cart's blob covers whoever sits in it.
    const auto cart = [&](const glm::vec3& middle) { casters.push_back(blob_caster{middle, style.cart_height, style.cart_radius}); };
    const auto figure = [&](const glm::vec3& feet) {
        casters.push_back(blob_caster{feet, style.player_height, style.player_radius});
    };
    if (on_foot(game)) {
        figure(game.player.position);
    }
    for (const render_cart& parked : data.carts) {
        cart(parked.position);
    }
    for (const render_remote_avatar& avatar : data.remote_avatars) {
        if (!avatar.seated) {
            figure(avatar.position);
        }
    }
    return casters;
}

std::vector<render_blob_shadow> frame_blob_shadows(const render_data& data, const game_state& game, const shadow_style& style) {
    std::vector<render_blob_shadow> blobs;
    const render_shadows& shadows = data.lighting.shadows;
    for (const blob_caster& caster : frame_casters(data, game, style)) {
        const glm::vec3 source = caster.bottom + world_up * caster.lift;
        const terrain_sample ground =
            cast_blob_onto_ground(game.area, source, data.lighting.sun_direction, shadows.sun_share, shadows.max_stretch);
        // With no sun only what touches the ground darkens it.
        const float height = std::max(0.0f, caster.bottom.y - terrain_height(game.area, caster.bottom));
        const float contact = glm::clamp(1.0f - height / style.contact_reach, 0.0f, 1.0f);
        blobs.push_back(render_blob_shadow{ground.point, ground.normal, 0.0f, caster.radius,
                                           style.blob_strength * glm::mix(contact, 1.0f, shadows.sun_share)});
    }
    return blobs;
}
}

terrain_sample cast_blob_onto_ground(const play_area& area, const glm::vec3& caster, const glm::vec3& towards_sun,
                                     const float sun_share, const float max_stretch) {
    const glm::vec2 level(towards_sun.x, towards_sun.z);
    const glm::vec2 way = glm::length(level) > 0.0001f ? glm::normalize(level) : glm::vec2(1.0f, 0.0f);
    const float sine = std::max(towards_sun.y, 1.0f / std::max(1.0f, max_stretch));
    const glm::vec2 run = way * (std::sqrt(1.0f - sine * sine) / sine);  // level metres per metre of drop
    const glm::vec2 under(caster.x, caster.z);
    glm::vec2 landing = under;
    for (int i = 0; i < cast_refinements; ++i) {
        const float drop = std::max(0.0f, caster.y - terrain_height(area, glm::vec3(landing.x, caster.y, landing.y)));
        landing = under - run * drop;
    }
    const glm::vec2 spot = glm::mix(under, landing, glm::clamp(sun_share, 0.0f, 1.0f));
    return sample_area(area, glm::vec3(spot.x, caster.y, spot.y));
}

void refresh_tree_shadows(tree_shadow_cache& cache, const game_state& game, const shadow_style& shadows) {
    if (cache.built && cache.revision == game.static_anchors.revision) {
        return;
    }
    std::vector<render_blob_shadow>& blobs = cache.blobs;
    blobs.clear();
    blobs.reserve(game.static_anchors.trees.size() + game.area.floodlight_poles.size());
    const auto blob_at = [&](const tree_body& body, const float radius) {
        const terrain_sample ground = sample_area(game.area, body.base);
        blobs.push_back(render_blob_shadow{body.base, ground.normal, 0.0f, radius, shadows.blob_strength * shadows.tree_contact_share});
    };
    for (const tree_body& tree : game.static_anchors.trees) {
        blob_at(tree, tree.shape.leaf_radius * shadows.tree_radius_share);
    }
    for (const tree_body& pole : game.area.floodlight_poles) {
        blob_at(pole, shadows.pole_radius);
    }
    cache.revision = game.static_anchors.revision;
    cache.built = true;
}

void add_frame_lights(render_data& data, const game_state& game, const lighting_palette& palette,
                      const tree_shadow_cache& trees) {
    data.glow = night_glow_floors(palette.colors.glow, palette.night_lights, data.lighting.night_lights);
    const night_light_style& style = palette.night_lights;
    data.lighting.lamp_glow = style.floodlight.color * style.lamp_glow * data.lighting.night_lights;
    if (!game.hub) {
        return;
    }
    data.lights = night_lights(data, game, palette.night_lights);
    data.blob_shadows = frame_blob_shadows(data, game, palette.shadows);
    data.tree_shadows = &trees.blobs;
    data.tree_shadows_revision = trees.revision;
}
