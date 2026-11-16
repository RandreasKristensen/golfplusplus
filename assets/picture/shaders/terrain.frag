#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "noise.glsl"

in vec3 v_normal;
in vec3 v_color;
in float v_rough;
in float v_paved;
// Mown grass: the mower's way, 2 long on a fairway and 1 on a green (zero unmown).
in vec2 v_mow;
// Metres across the line it was mown along: which pass it is in.
in float v_mow_across;
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
// Worn asphalt laid over the cart paths, a tint around mid grey like the grass.
uniform sampler2D u_path;
// The grass's look (grass_style): x the stripes' shade, y the patches'
// share, z their size in metres; the stripes' widths on fairways and greens.
uniform vec3 u_grass_look;
uniform vec2 u_mow_widths;

// Metres of ground one copy of the grass texture covers.
const float grass_tile_meters = 2.0;
// Metres of path one copy of the asphalt texture covers (tooling/art/make_art.py).
const float path_tile_meters = 1.5;
// How much of mown grass's wet shine the rough has.
const float rough_gloss_share = 0.4;

void main() {
    // Sampled before any branch so the mipmap choice stays well defined.
    vec3 grass = texture(u_grass, v_ground / grass_tile_meters).rgb * 2.0;
    vec3 asphalt = texture(u_path, v_ground / path_tile_meters).rgb * 2.0;
    if (u_use_vertex_color == 0) {
        frag_color = vec4(hazed(u_color * flat_light(u_glow), v_world), u_alpha);
        return;
    }

    float rough = clamp(v_rough, 0.0, 1.0);
    float paved = clamp(v_paved, 0.0, 1.0);
    // Each pass of the mower leans its blades the way it went, the next pass
    // back: leaning away from the eye they look lighter, towards it darker.
    // The stripes fade out where the mowing does (the fairway's edge).
    float mow_length = length(v_mow);
    bool on_fairway = mow_length > 1.5;
    float mow_width = on_fairway ? u_mow_widths.x : u_mow_widths.y;
    float mown = on_fairway ? smoothstep(1.5, 2.0, mow_length) : smoothstep(0.5, 1.0, mow_length);
    float stripe = 0.0;
    if (mown > 0.0) {
        vec2 way = v_mow / mow_length;
        // One pass, then the next back, softened over a pixel so the edge
        // between them stays straight in the small picture.
        float across = v_mow_across / mow_width;
        float zigzag = abs(fract(0.5 * across) * 2.0 - 1.0) - 0.5;
        float lean = clamp(zigzag / max(fwidth(across), 1.0e-4), -1.0, 1.0);
        vec2 from_eye = normalize(v_world.xz - haze_eye.xz + vec2(1.0e-5));
        stripe = mown * lean * dot(way, from_eye) * u_grass_look.x;
    }
    // Turf varies in patches, with soil and wear.
    float grassy = max(rough, mown) * (1.0 - paved);
    vec2 patch_at = v_world.xz / u_grass_look.z;
    float patch_noise = 0.65 * value_noise(patch_at) + 0.35 * value_noise(2.9 * patch_at + 17.0);
    float turf = 1.0 + grassy * u_grass_look.y * (2.0 * patch_noise - 1.0) + stripe;
    vec3 base_color = v_color * turf * mix(vec3(1.0), grass, rough) * mix(vec3(1.0), asphalt, paved) * wet.x;
    float sun = sun_shadow(v_horizon_a, v_horizon_b) * shadow_map_light(v_shadow);
    vec3 light = surface_light(v_normal, v_ao, sun) + local_light(v_world, v_normal);
    vec3 shine = wet_gloss(v_world, v_normal, sun) * mix(1.0, rough_gloss_share, rough) * v_ao;
    frag_color = vec4(hazed(base_color * light + shine, v_world), u_alpha);
}
