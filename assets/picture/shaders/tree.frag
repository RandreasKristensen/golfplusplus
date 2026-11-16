#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "noise.glsl"

// A tree's bark, or the leaves on one of its clumps' cards: a grid of leaves,
// each an ellipse at its own angle, as many of them as the crown is in leaf
// (u_foliage), so a bare crown shows its branches. Leaves inside the crown
// are shaded by those round them, and sun behind a leaf shines through it.
// Lit on the side the sun is on, in the shadow map's shade, hazed.

in vec3 v_world;
in vec3 v_normal;
in vec3 v_shadow;
in vec2 v_uv;
in float v_leaf;
in float v_depth;
flat in float v_tint;

out vec4 frag_color;

uniform vec3 u_bark_color;
uniform vec3 u_leaf_color;
uniform float u_foliage;
uniform float u_shine_through;  // the share of the sun behind a leaf that comes through it

// Leaves across a card, each way.
const float leaves_across = 5.0;

void main() {
    vec3 n = normalize(v_normal);
    float sun = shadow_map_light(v_shadow);
    vec3 color = u_bark_color * (0.85 + 0.3 * v_tint);
    float ao = 1.0;
    vec3 shine = vec3(0.0);
    if (v_leaf > 0.5) {
        vec2 grid = v_uv * leaves_across;
        vec2 cell = floor(grid) + v_tint * 37.0;
        if (hash12(cell) >= u_foliage) {
            discard;
        }
        float angle = 6.2831853 * hash12(cell + 3.1);
        vec2 f = fract(grid) - 0.5;
        vec2 r = vec2(cos(angle) * f.x + sin(angle) * f.y, -sin(angle) * f.x + cos(angle) * f.y) / vec2(0.5, 0.36);
        if (dot(r, r) > 1.0) {
            discard;
        }
        color = u_leaf_color * (0.8 + 0.4 * hash12(cell + 7.7)) * (0.9 + 0.2 * v_tint);
        ao = mix(1.0, 0.45, v_depth);
        vec3 to_eye = normalize(haze_eye.xyz - v_world);
        float behind = max(dot(-to_eye, sun_direction.xyz), 0.0);
        shine = sun_color.rgb * color * (u_shine_through * behind * behind * sun * (1.0 - v_depth));
    }
    n = dot(n, sun_direction.xyz) < 0.0 && v_leaf > 0.5 ? mix(n, -n, 0.3) : n;
    vec3 light = surface_light(n, ao, sun) + local_light(v_world, n);
    frag_color = vec4(hazed(color * light * wet.x + shine, v_world), 1.0);
}
