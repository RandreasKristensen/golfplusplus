#version 330 core

#include "lighting.glsl"

// The scene seen from under a pond's surface: one murky colour over it, as
// dark as the light above.

out vec4 frag_color;

uniform vec3 u_color;
uniform float u_alpha;

void main() {
    frag_color = vec4(u_color * flat_light(0.0), u_alpha);
}
