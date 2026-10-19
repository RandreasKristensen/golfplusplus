#version 330 core

#include "lighting.glsl"

in vec3 v_normal;
in vec3 v_color;
in float v_rough;
in float v_ao;
in vec4 v_horizon_a;
in vec4 v_horizon_b;
in vec2 v_ground;
// Per fragment: the backdrop ground is two huge triangles.
in vec3 v_world;
in vec3 v_shadow;

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_alpha;
// Flat draws (backdrop ground, emote props, flight paths): their glow floor.
uniform float u_glow;
uniform int u_use_vertex_color;
// Blades of grass laid over the rough. A tint around mid grey: 128 leaves the
// colour as it is, darker and lighter strokes shade it.
uniform sampler2D u_grass;
// The trees' shadow map (renderer/shadow_map_pass.h). x: how much sun their
// shadow takes away, 0 with no map; y: its taps' spread, in map widths; z:
// its depth bias; w: the share of the map's half-width its edge fades over.
uniform sampler2DShadow u_shadow_map;
uniform vec4 u_shadow;

// Metres of ground one copy of the grass texture covers.
const float grass_tile_meters = 2.0;

// How much sun the trees let through to here, 0..1: four soft taps on a
// turned square, each already a 2x2 filtered test.
float tree_light() {
    vec2 from_middle = abs(v_shadow.xy * 2.0 - 1.0);
    float inside = (1.0 - smoothstep(1.0 - u_shadow.w, 1.0, max(from_middle.x, from_middle.y))) *
        step(0.0, v_shadow.z) * step(v_shadow.z, 1.0);
    float depth = v_shadow.z - u_shadow.z;
    float spread = u_shadow.y;
    float lit = 0.25 * (texture(u_shadow_map, vec3(v_shadow.xy + vec2(-0.4, -1.0) * spread, depth)) +
                        texture(u_shadow_map, vec3(v_shadow.xy + vec2(1.0, -0.4) * spread, depth)) +
                        texture(u_shadow_map, vec3(v_shadow.xy + vec2(-1.0, 0.4) * spread, depth)) +
                        texture(u_shadow_map, vec3(v_shadow.xy + vec2(0.4, 1.0) * spread, depth)));
    return 1.0 - u_shadow.x * inside * (1.0 - lit);
}

void main() {
    // Sampled before any branch so the mipmap choice stays well defined.
    vec3 grass = texture(u_grass, v_ground / grass_tile_meters).rgb * 2.0;
    if (u_use_vertex_color == 0) {
        frag_color = vec4(hazed(u_color * flat_light(u_glow), v_world), u_alpha);
        return;
    }

    vec3 base_color = v_color * mix(vec3(1.0), grass, clamp(v_rough, 0.0, 1.0));
    vec3 light = surface_light(v_normal, v_ao, sun_shadow(v_horizon_a, v_horizon_b) * tree_light()) + local_light(v_world, v_normal);
    frag_color = vec4(hazed(base_color * light, v_world), u_alpha);
}
