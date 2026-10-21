#version 330 core

#include "lighting.glsl"

// A streak of rain or a speck blown by the wind (renderer/weather_renderer.h):
// a line from where it is now back along its motion. Each has its own place
// in a box round the camera that moves with the weather and wraps, so the
// box never ends and nothing is rebuilt; fading out towards the box's edge
// and very near the camera hides the wrap. Lit by the light there is: a
// speck as an unlit thing, a drop of water by the sky's light seen through
// it (the haze), so rain is pale by day, glints by the night lights and is
// all but gone in the dark.

layout(location = 0) in vec4 a_seed;  // xyz: its place in the box, 0..1; w: its own random 0..1
layout(location = 1) in float a_end;  // 0 where it is, 1 its tail

uniform mat4 u_view_proj;
uniform vec3 u_center;
uniform vec3 u_size;
uniform vec3 u_offset;
uniform vec3 u_velocity;
uniform vec3 u_color;
uniform float u_water;  // 1 for a drop of water, 0 for a speck
// x: the streak's seconds of motion; y: its alpha; z: the near fade in metres.
uniform vec3 u_streak;
// x: the bob in metres; y: its phase in radians.
uniform vec2 u_tumble;

out vec4 v_color;

const float full_turn = 6.28318530718;

void main() {
    vec3 local = fract(a_seed.xyz + (u_offset - u_center) / u_size) - 0.5;
    vec3 head = u_center + local * u_size;
    head.y += u_tumble.x * sin(u_tumble.y + full_turn * a_seed.w);
    vec3 world = head - u_velocity * (u_streak.x * a_end);

    float edge = 2.0 * max(max(abs(local.x), abs(local.z)), abs(local.y));
    float near = smoothstep(0.0, u_streak.z, distance(head, haze_eye.xyz));
    float fade = (1.0 - smoothstep(0.6, 1.0, edge)) * near * mix(0.55, 1.0, a_seed.w) * mix(1.0, 0.5, a_end);
    vec3 color = u_color * (mix(flat_light(0.0), haze_color.rgb, u_water) + local_light(world));
    v_color = vec4(hazed(color, world), u_streak.y * fade);
    gl_Position = u_view_proj * vec4(world, 1.0);
}
