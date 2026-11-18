#version 330 core

// Solid casters keep depth; trees keep only their visible leaves and bark.
#ifdef TREE_CASTER
#include "noise.glsl"
#include "tree_leaf.glsl"
in vec2 v_uv;
in float v_leaf;
flat in float v_tint;
uniform float u_foliage;
#endif

void main() {
#ifdef TREE_CASTER
    if (v_leaf > 0.5 && !tree_leaf_present(v_uv, v_tint, u_foliage)) {
        discard;
    }
#endif
}
