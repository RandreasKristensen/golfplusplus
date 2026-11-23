#version 330 core

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "noise.glsl"

in vec3 v_normal;
in vec3 v_color;
in float v_rough;
in float v_paved;
// Short turf: 1 on a fairway, 2 on a green, 0 elsewhere, between where they meet.
in float v_turf;
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
// The rough's patches (grass_style): x their share of its colour, y their size in metres.
uniform vec2 u_patch_look;
// Short turf's fibre (grass_style), fairways and greens: x the tufts' size in
// metres, y their colour's share, z the slope of their sides.
uniform vec3 u_fibre_fairway;
uniform vec3 u_fibre_green;
// The most light the night lights give the ground, rolled off towards it (grass_style).
uniform float u_lamp_cap;

// Metres of ground one copy of the grass texture covers.
const float grass_tile_meters = 2.0;
// Metres of path one copy of the asphalt texture covers (tooling/art/make_art.py).
const float path_tile_meters = 1.5;
// How much of mown grass's wet shine the rough has.
const float rough_gloss_share = 0.4;

// Tufts are two octaves of noise, the second this much finer; a tuft is
// drawn once the pixel is a third of its size and has faded out by the time
// the pixel covers it.
const float fibre_fine_octave = 2.3;
const float fibre_resolved_cells = 0.35;
const float fibre_gone_cells = 1.0;
const float fibre_fine_weight = 0.35;

// An octave that has faded out is not sampled.
float tuft_height(vec2 cell, float coarse_seen, float fine_seen) {
    float height = 0.0;
    if (coarse_seen > 0.0) {
        height += (1.0 - fibre_fine_weight) * coarse_seen * (2.0 * value_noise(cell) - 1.0);
    }
    if (fine_seen > 0.0) {
        height += fibre_fine_weight * fine_seen * (2.0 * value_noise(fibre_fine_octave * cell + 11.0) - 1.0);
    }
    return height;
}

// Short turf's tufts at `at` for tufts `meters` across seen from a pixel
// `pixel_meters` wide, fixed in the world: x their height, -1..1; yz the
// slope of their sides along x and z (what a pixel can show of them); w their
// mean squared slope, resolved or not, for the wet shine to spread over.
vec4 turf_fibre(vec2 at, float meters, float pixel_meters, float slope) {
    float cells = pixel_meters / meters;
    float coarse_seen = 1.0 - smoothstep(fibre_resolved_cells, fibre_gone_cells, cells);
    float fine_seen = 1.0 - smoothstep(fibre_resolved_cells, fibre_gone_cells, fibre_fine_octave * cells);
    vec2 cell = at / meters;
    const float step_cells = 0.3;
    float height = tuft_height(cell, coarse_seen, fine_seen);
    float along_x = tuft_height(cell + vec2(step_cells, 0.0), coarse_seen, fine_seen);
    float along_z = tuft_height(cell + vec2(0.0, step_cells), coarse_seen, fine_seen);
    vec2 sides = slope * vec2(along_x - height, along_z - height) / step_cells;
    float coarse_weight = 1.0 - fibre_fine_weight;
    float fine_weight = fibre_fine_weight * fibre_fine_octave;
    return vec4(height, sides, slope * slope * (coarse_weight * coarse_weight + fine_weight * fine_weight));
}

// `normal` tilted by `sides`, the ground's slope along x and z, the part
// across it dropped.
vec3 tilted(vec3 normal, vec2 sides) {
    vec3 grade = vec3(sides.x, 0.0, sides.y);
    return normalize(normal - (grade - normal * dot(grade, normal)));
}

// `lamp` (the night lights' light on the ground) rolled off towards `cap`
// from its half, keeping its colour: lamps overlapping on pale ground never
// light it past white.
vec3 capped_lamp_light(vec3 lamp, float cap) {
    float peak = max(max(lamp.r, lamp.g), lamp.b);
    float knee = 0.5 * cap;
    float kept = min(peak, knee) + (cap - knee) * (1.0 - exp(-max(peak - knee, 0.0) / (cap - knee)));
    return lamp * (kept / max(peak, 1.0e-4));
}

void main() {
    // Sampled before any branch so the mipmap choice stays well defined.
    vec3 grass = texture(u_grass, v_ground / grass_tile_meters).rgb * 2.0;
    vec3 asphalt = texture(u_path, v_ground / path_tile_meters).rgb * 2.0;
    float pixel_meters = max(length(dFdx(v_world.xz)), length(dFdy(v_world.xz)));
    if (u_use_vertex_color == 0) {
        frag_color = vec4(hazed(u_color * flat_light(u_glow), v_world), u_alpha);
        return;
    }

    float rough = clamp(v_rough, 0.0, 1.0);
    float paved = clamp(v_paved, 0.0, 1.0);
    // Short turf, finer and calmer on a green than on a fairway.
    float short_turf = clamp(v_turf, 0.0, 1.0) * (1.0 - paved);
    vec3 fibre_look = mix(u_fibre_fairway, u_fibre_green, clamp(v_turf - 1.0, 0.0, 1.0));
    vec4 fibre = short_turf > 0.0 ? turf_fibre(v_world.xz, fibre_look.x, pixel_meters, fibre_look.z) : vec4(0.0);
    // The rough varies in patches, with soil and wear.
    float patch_noise = 0.5;
    if (rough * (1.0 - paved) > 0.0) {
        vec2 patch_at = v_world.xz / u_patch_look.y;
        patch_noise = 0.65 * value_noise(patch_at) + 0.35 * value_noise(2.9 * patch_at + 17.0);
    }
    float turf = 1.0 + rough * (1.0 - paved) * u_patch_look.x * (2.0 * patch_noise - 1.0) +
                 short_turf * fibre_look.y * fibre.x;
    vec3 base_color = v_color * turf * mix(vec3(1.0), grass, rough) * mix(vec3(1.0), asphalt, paved) * wet.x;
    vec3 normal = tilted(normalize(v_normal), short_turf * fibre.yz);
    float sun = sun_shadow(v_horizon_a, v_horizon_b) * shadow_map_light(v_shadow);
    vec3 light = surface_light(normal, v_ao, sun) + capped_lamp_light(local_light(v_world, normal), u_lamp_cap);
    vec3 shine = vec3(0.0);
    if (wet.y > 0.0) {
        shine = wet_gloss(v_world, normalize(v_normal), sun, short_turf * fibre.w) * mix(1.0, rough_gloss_share, rough) * v_ao;
    }
    frag_color = vec4(hazed(base_color * light + shine, v_world), u_alpha);
}
