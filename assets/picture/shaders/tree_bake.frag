#version 330 core

#include "noise.glsl"
#include "tree_leaf.glsl"

// A tree's unlit surface for the image grid, drawn at several times a tile's
// resolution and averaged down into it (tree_bake_resolve.frag), so a texel's
// coverage is the real share of it that leaves, twigs and bark cover, even
// twigs far finer than a tile's texel: colour with coverage, the unit-frame
// normal with the depth along the view, and whether it is a leaf (red: 1, a
// twig or bark 0) and how far out in the crown (green: 1 at its surface and on
// bark .. 0 at its middle), each its own channel so an averaged texel stays
// both true.
// tree.frag's colours at the middle tint; an instance's tint is applied when
// the image is drawn (tree_image.frag).

in vec3 v_normal;
in vec2 v_uv;
in float v_leaf;
in float v_depth;
in float v_ray;
flat in vec2 v_card;

layout(location = 0) out vec4 o_albedo;
layout(location = 1) out vec4 o_normal;
layout(location = 2) out vec4 o_leaf;

uniform vec3 u_bark_color;
uniform vec3 u_twig_color;
uniform vec3 u_leaf_color;
uniform vec3 u_withered_color;
uniform vec2 u_season;
uniform vec4 u_twigs;

const float baked_tint = 0.5;
const float bark_fineness = 0.6;  // tree.frag's

void main() {
    float pixel = tree_card_pixel(v_uv, u_twigs.z);
    vec3 color = mix(u_bark_color, u_twig_color, bark_fineness * v_depth);
    // Bark is at the crown's surface as far as its shading goes.
    vec2 kind = vec2(0.0, 1.0);
    if (v_leaf > 0.5) {
        float shade;
        int texel = tree_card_texel(v_uv, baked_tint, v_card.y, v_card.x, u_season, u_twigs, pixel, shade);
        if (texel == card_empty) {
            discard;
        }
        if (texel == card_twig) {
            color = u_twig_color;
            kind = vec2(0.0, 1.0 - v_depth);
        } else {
            color = (texel == card_leaf ? u_leaf_color : u_withered_color) * (0.8 + 0.4 * shade);
            kind = vec2(1.0, 1.0 - v_depth);
        }
    }
    o_albedo = vec4(color, 1.0);
    o_normal = vec4(normalize(v_normal) * 0.5 + 0.5, v_ray);
    o_leaf = vec4(kind, 0.0, 0.0);
}
