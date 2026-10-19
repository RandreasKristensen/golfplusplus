#version 330 core

#include "lighting.glsl"

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_glow;

in vec3 v_normal;

void main() {
    frag_color = vec4(u_color * max(surface_light(v_normal, 1.0), vec3(u_glow)), 1.0);
}
