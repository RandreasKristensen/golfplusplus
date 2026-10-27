#include "core/light_frame.h"

#include "game/golf_cart.h"
#include "game/play_area.h"
#include "game/sun_lighting.h"
#include "physics/vector_math.h"
#include "renderer/cart_batch.h"

#include <algorithm>
#include <cmath>

#include <glm/common.hpp>
#include <glm/geometric.hpp>
#include <glm/mat4x4.hpp>
#include <glm/trigonometric.hpp>
#include <glm/vec2.hpp>

namespace {
// Each guess at where the cast lands is corrected by the ground's height
// there this many times, enough on rolling ground.
constexpr int cast_refinements = 3;

// Something that casts a blob: its lowest point, how high over that it is
// cast from (0: a blob at its foot, its shadow the shadow map's), and how
// wide its blob is.
struct blob_caster {
    glm::vec3 bottom{0.0f};
    float lift = 0.0f;
    float radius = 0.0f;
};

// The way towards the sun a blob is cast along: never lower than 1 / max_stretch.
glm::vec3 casting_sun(const glm::vec3& towards_sun, const float max_stretch) {
    const glm::vec2 level(towards_sun.x, towards_sun.z);
    const glm::vec2 way = glm::length(level) > 0.0001f ? glm::normalize(level) : glm::vec2(1.0f, 0.0f);
    const float sine = std::max(towards_sun.y, 1.0f / std::max(1.0f, max_stretch));
    const float cosine = std::sqrt(1.0f - sine * sine);
    return glm::vec3(way.x * cosine, sine, way.y * cosine);
}

// How much longer than wide a round thing's shadow lies along the sun on
// ground facing `normal`: one over the sine of the sun above that ground.
// Long enough that a ball on the ground still sits in its own shadow.
float blob_stretch(const glm::vec3& towards_sun, const glm::vec3& normal, const float max_stretch) {
    const float limit = std::max(1.0f, max_stretch);
    return std::min(1.0f / std::max(glm::dot(casting_sun(towards_sun, limit), normal), 1.0f / limit), limit);
}

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

// A cart's headlight, tilted with the cart.
render_light headlight(const render_cart& cart, const night_light_style& style, const float share) {
    const glm::mat4 frame = cart_frame(cart);
    const glm::vec3 up(frame[1]);
    const glm::vec3 forward(frame[2]);
    const float pitch = glm::radians(style.headlight_pitch_deg);
    return spot_light(cart.position + forward * style.headlight_forward + up * style.headlight_height,
                      glm::normalize(forward * std::cos(pitch) - up * std::sin(pitch)), style.headlight, share,
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
            lights.push_back(headlight(cart, style, share));
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
    const auto cart = [&](const glm::vec3& middle) { casters.push_back(blob_caster{middle, 0.0f, style.cart_radius}); };
    const auto figure = [&](const glm::vec3& feet) { casters.push_back(blob_caster{feet, 0.0f, style.player_radius}); };
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
        const float stretch = caster.lift > 0.0f
            ? glm::mix(1.0f, blob_stretch(data.lighting.sun_direction, ground.normal, shadows.max_stretch), shadows.sun_share)
            : 1.0f;
        // A blob at a foot is fainter by day, when the shadow map casts the rest.
        const float strength = caster.lift > 0.0f ? glm::mix(contact, 1.0f, shadows.sun_share)
                                                  : contact * glm::mix(1.0f, style.foot_share, shadows.sun_share);
        blobs.push_back(render_blob_shadow{ground.point, ground.normal, stretch, caster.radius, style.blob_strength * strength});
    }
    return blobs;
}
}

terrain_sample cast_blob_onto_ground(const play_area& area, const glm::vec3& caster, const glm::vec3& towards_sun,
                                     const float sun_share, const float max_stretch) {
    const glm::vec3 sun = casting_sun(towards_sun, max_stretch);
    const glm::vec2 run = glm::vec2(sun.x, sun.z) / sun.y;  // level metres per metre of drop
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
    blobs.reserve(game.static_anchors.trees.size() + game.area.floodlights.size());
    const auto blob_at = [&](const glm::vec3& foot, const float radius) {
        const terrain_sample ground = sample_area(game.area, foot);
        blobs.push_back(render_blob_shadow{foot, ground.normal, 1.0f, radius, shadows.blob_strength * shadows.foot_share});
    };
    for (const tree_body& tree : game.static_anchors.trees) {
        blob_at(tree.base, tree.shape.leaf_radius * shadows.tree_radius_share);
    }
    for (const floodlight& light : game.area.floodlights) {
        blob_at(light.foot, shadows.pole_radius);
    }
    cache.revision = game.static_anchors.revision;
    cache.built = true;
}

void add_frame_lights(render_data& data, const game_state& game, const lighting_palette& palette,
                      const tree_shadow_cache& trees) {
    data.glow = night_glow_floors(palette.colors.glow, palette.night_lights, data.lighting.night_lights);
    const night_light_style& style = palette.night_lights;
    data.lighting.lamp_glow = style.floodlight.color * style.lamp_glow * data.lighting.night_lights;
    if (!course_started(game)) {
        return;
    }
    data.lights = night_lights(data, game, palette.night_lights);
    data.blob_shadows = frame_blob_shadows(data, game, palette.shadows);
    data.tree_shadows = &trees.blobs;
    data.tree_shadows_revision = trees.revision;
}
