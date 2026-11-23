#version 330 core

// Solid casters keep depth; trees keep only their visible leaves and bark.
#ifdef TREE_CASTER
#include "noise.glsl"
#include "tree_leaf.glsl"
in vec2 v_uv;
in float v_leaf;
flat in vec2 v_card;
flat in float v_tint;
uniform vec2 u_season;
uniform vec4 u_twigs;
#endif

void main() {
#ifdef TREE_CASTER
    float pixel = tree_card_pixel(v_uv, u_twigs.z);
    float shade;
    if (v_leaf > 0.5 && tree_card_texel(v_uv, v_tint, v_card.y, v_card.x, u_season, u_twigs, pixel, shade) == card_empty) {
        discard;
    }
#endif
}
