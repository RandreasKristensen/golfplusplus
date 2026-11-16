#version 330 core

#include "lighting.glsl"

// The floodlights (floodlight.vert): lit_flat.frag's flat colour, plus the
// lens's glow, which falls off across the face rather than per triangle.

flat in vec4 v_color;
in vec3 v_glow;
in vec3 v_world;

out vec4 frag_color;

void main() {
    frag_color = vec4(v_color.rgb + v_glow + scattered_light(v_world), v_color.a);
}
