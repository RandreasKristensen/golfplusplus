#version 330 core

#include "lighting.glsl"

// A pond's surface: its colour over what lies under it, as bright as the
// light there is, fading into the haze with distance.

in vec3 v_world;

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_alpha;

void main() {
    frag_color = vec4(hazed(u_color * flat_light(0.0), v_world), u_alpha);
}
