#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "noise.glsl"
#include "tree_leaf.glsl"
#include "tree_light.glsl"

// A tree's bark, or one of its clumps' cards: leaves as many as the crown is
// in leaf (u_season.x), withered ones it keeps (u_season.y) and the fine
// twigs between them, so a bare crown is a haze of twigs round its branches
// (tree_leaf.glsl). Leaves inside the crown are shaded by those round them,
// and sun behind a leaf shines through it; twigs are lit as bark. Lit on the
// side the sun is on, in the shadow map's shade, hazed.

in vec3 v_world;
in vec3 v_normal;
in vec3 v_shadow;
in vec2 v_uv;
in float v_leaf;
in float v_depth;
flat in vec2 v_card;
flat in float v_tint;

out vec4 frag_color;

uniform vec3 u_bark_color;
uniform vec3 u_twig_color;
uniform vec3 u_leaf_color;
uniform vec3 u_withered_color;
uniform vec2 u_season;  // the crown's share in leaf and its withered share
uniform vec4 u_twigs;   // zig-zag, hang, the cards' length over width, the finest twigs' haze
uniform float u_shine_through;  // the share of the sun behind a leaf that comes through it

const float bark_fineness = 0.6;

void main() {
    float pixel = tree_card_pixel(v_uv, u_twigs.z);
    float sun = shadow_map_light(v_shadow);
    // Finer branches take on the twigs' colour (bark's v_depth is its order).
    vec3 color = mix(u_bark_color, u_twig_color, bark_fineness * v_depth) * (0.85 + 0.3 * v_tint);
    float kind = 0.0;
    if (v_leaf > 0.5) {
        float shade;
        int texel = tree_card_texel(v_uv, v_tint, v_card.y, v_card.x, u_season, u_twigs, pixel, shade);
        if (texel == card_empty) {
            discard;
        }
        if (texel == card_twig) {
            color = u_twig_color * (0.85 + 0.3 * v_tint);
            kind = 0.5;
        } else {
            color = (texel == card_leaf ? u_leaf_color : u_withered_color) * (0.8 + 0.4 * shade) * (0.9 + 0.2 * v_tint);
            kind = 1.0;
        }
    }
    frag_color = vec4(tree_lit(color, normalize(v_normal), kind, v_depth, v_world, sun, u_shine_through), 1.0);
}
