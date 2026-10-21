#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"

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

// Metres of ground one copy of the grass texture covers.
const float grass_tile_meters = 2.0;
// How much of mown grass's wet shine the rough has.
const float rough_gloss_share = 0.4;

void main() {
    // Sampled before any branch so the mipmap choice stays well defined.
    vec3 grass = texture(u_grass, v_ground / grass_tile_meters).rgb * 2.0;
    if (u_use_vertex_color == 0) {
        frag_color = vec4(hazed(u_color * flat_light(u_glow), v_world), u_alpha);
        return;
    }

    float rough = clamp(v_rough, 0.0, 1.0);
    vec3 base_color = v_color * mix(vec3(1.0), grass, rough) * wet.x;
    float sun = sun_shadow(v_horizon_a, v_horizon_b) * shadow_map_light(v_shadow);
    vec3 light = surface_light(v_normal, v_ao, sun) + local_light(v_world, v_normal);
    vec3 shine = wet_gloss(v_world, v_normal, sun) * mix(1.0, rough_gloss_share, rough) * v_ao;
    frag_color = vec4(hazed(base_color * light + shine, v_world), u_alpha);
}
