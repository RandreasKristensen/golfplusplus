#version 330 core

// The shafts at half the scene's size (renderer/sun_shaft_pass.h), what is
// in shadow taken out of the haze. SUN_SHAFTS: along each pixel's ray,
// SHAFT_STEPS steps through the haze, each reading the shadow map once; the
// haze a step adds that lies in shadow sends the eye none of the sun's glow
// the scene was drawn with. LAMP_SHADOWS: for each of that many floodlights,
// SHAFT_STEPS steps through the run of the ray in its beam, evenly in the
// angle the lamp sees it under as scattered_from samples it, each reading
// the lamp's map once; the share of the beam's light from air in shadow is
// taken out of what scattered_from gave (renderer/scattered_light.h's
// lamp_shaft_shade). rgb: the light to take out of the scene; a: how far the
// ray went, for the upsample. Each pixel's steps start a 4x4 Bayer share of
// a step in, so 16 steps look like many more; the tape's grain hides the
// pattern.

#include "lighting.glsl"
#include "shadow_map_light.glsl"
#include "scene_ray.glsl"

out vec4 frag_color;

uniform mat4 u_shadow_view_proj;

const float bayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

#ifdef LAMP_SHADOWS
// The floodlights' maps, LAMP_TILES square tiles side by side
// (renderer/lamp_shadow_pass.h).
uniform sampler2DShadow u_lamp_shadow_map;
uniform mat4 u_lamp_view_proj[LAMP_SHADOWS];
// x: its light in frame_lighting; y: its tile; z: its depth bias, over the
// square of how far along its beam.
uniform vec4 u_lamp[LAMP_SHADOWS];

// How much of lamp `s`'s light its map lets through to `point`, 0..1, in one
// tap; all of it outside the map.
float lamp_light_once(int s, vec3 point) {
    vec4 clip = u_lamp_view_proj[s] * vec4(point, 1.0);
    float w = max(clip.w, 1.0e-4);
    vec3 coords = clip.xyz / w * 0.5 + 0.5;
    vec3 inside = step(vec3(0.0), coords) * step(coords, vec3(1.0));
    float half_texel = 0.5 / float(textureSize(u_lamp_shadow_map, 0).y);
    vec2 at = clamp(coords.xy, vec2(half_texel), vec2(1.0 - half_texel));
    float lit = texture(u_lamp_shadow_map, vec3((at.x + u_lamp[s].y) / float(LAMP_TILES), at.y,
                                                coords.z - u_lamp[s].z / (w * w)));
    return mix(1.0, lit, inside.x * inside.y * inside.z * step(0.0, clip.w));
}

// The lamps' beams' light from air in their shadow along `ray` (the way and
// metres), each step `offset` of the way through it.
vec3 lamp_shade(vec4 ray, float offset) {
    vec3 shade = vec3(0.0);
    for (int s = 0; s < LAMP_SHADOWS; ++s) {
        int i = int(u_lamp[s].x);
        vec4 beam = beam_run(i, ray.xyz, ray.w);
        if (beam.w <= beam.z) {
            continue;
        }
        float all_air = 0.0;
        float shaded = 0.0;
        for (int k = 0; k < SHAFT_STEPS; ++k) {
            vec3 point = beam_point(beam, ray.xyz, (float(k) + offset) / float(SHAFT_STEPS));
            vec3 air = beam_air(i, point, ray.xyz);
            float weight = air.r + air.g + air.b;
            all_air += weight;
            shaded += weight * (1.0 - lamp_light_once(s, point));
        }
        if (all_air > 0.0) {
            shade += scattered_from(i, ray.xyz, ray.w) * (shaded / all_air);
        }
    }
    return shade;
}
#endif

void main() {
    ivec2 texel = min(ivec2(gl_FragCoord.xy) * 2, textureSize(u_depth, 0) - 1);
    vec4 ray = scene_ray(texel);
    ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
    float offset = (bayer[cell.y * 4 + cell.x] + 0.5) / 16.0;
    vec3 taken = vec3(0.0);
#ifdef SUN_SHAFTS
    float step_meters = ray.w / float(SHAFT_STEPS);
    float shaded = 0.0;
    float before = 0.0;
    for (int k = 0; k < SHAFT_STEPS; ++k) {
        float after = haze_over(ray.xyz, step_meters * float(k + 1));
        vec3 point = haze_eye.xyz + ray.xyz * (step_meters * (float(k) + offset));
        vec3 coords = (u_shadow_view_proj * vec4(point, 1.0)).xyz * 0.5 + 0.5;
        shaded += (after - before) * (1.0 - shadow_map_light_once(coords));
        before = after;
    }
    taken += haze_sun_glow(ray.xyz) * shaded;
#endif
#ifdef LAMP_SHADOWS
    taken += lamp_shade(ray, offset);
#endif
    frag_color = vec4(taken, ray.w);
}
