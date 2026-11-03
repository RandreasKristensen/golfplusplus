#version 330 core

#include "lighting.glsl"

// A flat colour lit and faded into the haze per vertex (trees, floodlights,
// world markers),
// with the night lights' light in the air added per pixel: it changes too
// fast across a triangle to take from a corner.

flat in vec4 v_color;
in vec3 v_world;

out vec4 frag_color;

void main() {
    frag_color = vec4(v_color.rgb + scattered_light(v_world), v_color.a);
}
