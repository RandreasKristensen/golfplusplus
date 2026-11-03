#version 330 core

#include "lighting.glsl"

// The ball: lit, never darker than its glow floor, faded into the haze like
// everything else.

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_glow;

in vec3 v_normal;
in vec3 v_world;

void main() {
    frag_color = vec4(hazed(u_color * max(surface_light(v_normal, 1.0), vec3(u_glow)), v_world), 1.0);
}
