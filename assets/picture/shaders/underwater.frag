#version 330 core

#include "lighting.glsl"

// The scene seen from under a pond's surface, a copy of it read back: each
// pixel fogged along its path from the eye by the water's absorption and
// scatter (the same numbers as water.frag: what is lost is replaced by the
// scatter colour, lit by the light that has got down to the eye's depth), sun caustics on what lies under the
// surface, and a slow wobble of the view. Mirrors renderer/water_light.h.
// The pond surface seen from below is fogged here like everything else.

out vec4 frag_color;

uniform sampler2D u_scene_color;
uniform sampler2D u_scene_depth;
uniform mat4 u_inverse_view_proj;
uniform float u_time;
uniform vec3 u_absorption;       // per metre, per channel
uniform vec3 u_scatter_color;
uniform vec3 u_water;            // x: longest path; y: caustic strength; z: caustic cell metres
uniform vec2 u_pond;             // x: the surface's height; y: the wobble, a share of the picture height

const float tau = 6.2831853;
const float far_meters = 1.0e6;

vec3 world_at(vec2 uv, float depth) {
    vec4 point = u_inverse_view_proj * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return point.xyz / point.w;
}

float eye_meters(vec3 point, float depth) {
    return depth >= 1.0 ? far_meters : distance(haze_eye.xyz, point);
}

// Bright thin ridges where two drifting sets of waves cross.
float caustic_pattern(vec2 ground, float cell) {
    vec2 q = ground / cell;
    float a = 1.0 - abs(sin(dot(q, vec2(0.9, 0.4)) * 3.1416 + u_time * 0.8 + sin(q.y * 2.1 + u_time * 0.5)));
    float b = 1.0 - abs(sin(dot(q, vec2(-0.5, 0.85)) * 3.1416 - u_time * 0.6 + sin(q.x * 1.7 - u_time * 0.4)));
    return pow(a * b, 3.0) * 4.0;
}

void main() {
    ivec2 size = textureSize(u_scene_color, 0);
    ivec2 texel = min(ivec2(gl_FragCoord.xy), size - 1);
    vec2 uv = (vec2(texel) + 0.5) / vec2(size);
    float depth = texelFetch(u_scene_depth, texel, 0).r;
    vec3 point = world_at(uv, depth);

    // A slow, smooth drift of the lookup; a sample much nearer than this
    // pixel is something else in front of it, so it is left out.
    vec2 drift = vec2(sin(uv.y * 9.0 + u_time * 0.7) + 0.5 * sin(uv.y * 23.0 - u_time * 0.5),
                      cos(uv.x * 7.0 + u_time * 0.6) + 0.5 * cos(uv.x * 19.0 - u_time * 0.8)) / 1.5;
    vec2 shifted = uv + drift * u_pond.y * vec2(float(size.y) / float(size.x), 1.0);
    ivec2 other = clamp(ivec2(shifted * vec2(size)), ivec2(0), size - 1);
    float other_depth = texelFetch(u_scene_depth, other, 0).r;
    vec3 other_point = world_at((vec2(other) + 0.5) / vec2(size), other_depth);
    if (eye_meters(other_point, other_depth) >= eye_meters(point, depth) - 0.3) {
        texel = other;
        depth = other_depth;
        point = other_point;
    }

    vec3 color = texelFetch(u_scene_color, texel, 0).rgb;
    // The way the surface here faces, towards the eye, from the depth's slope.
    vec3 across = cross(dFdx(point), dFdy(point));
    vec3 facing = dot(across, haze_eye.xyz - point) < 0.0 ? -across : across;
    float up = max(facing.y, 0.0) / max(length(facing), 1.0e-12);

    float under_surface = u_pond.x - point.y;
    if (depth < 1.0 && under_surface > 0.05) {
        float sun_up = smoothstep(0.0, 0.1, sun_direction.y);
        vec3 shimmer = sun_color.rgb * (u_water.y * sun_up * up * exp(-u_absorption.g * under_surface)) *
            caustic_pattern(point.xz, u_water.z);
        color += color * shimmer;
    }

    // Fog along the eye's path, scattering the light that reaches the eye's depth.
    float path = min(eye_meters(point, depth), u_water.x);
    vec3 light = flat_light(0.0) * exp(-u_absorption * max(u_pond.x - haze_eye.y, 0.0));
    color = color * exp(-u_absorption * path) + u_scatter_color * light * (1.0 - exp(-u_absorption * path));
    frag_color = vec4(color, 1.0);
}
